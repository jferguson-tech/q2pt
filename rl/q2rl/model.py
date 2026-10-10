# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Jonathan Ferguson
"""The policy: what the player perceives in, one choice per action branch out.

The view is partial, so the network is recurrent. Each part of the
observation has an encoder of its own:

    self, guide     scaled and passed through
    rays            a 9 x 24 picture of 12 planes (nearness, slope, and the
                    kind of thing hit as 10 one-hot planes), through two
                    convolutions
    things in view  each of the 16 rows through the same small network, then
                    the largest value of each feature over the rows, so that
                    their order does not matter
    last action     one-hot per branch

These are joined, mixed by one layer, and fed to a GRU. Seven heads give the
logits of the action's branches and one more the value.

The observation arrives raw, as the game gives it; `Features` holds the
scaling, so that data on disk does not depend on it.
"""

import torch
import torch.nn as nn
import torch.nn.functional as F

from q2env import layout as L

ITEM_TYPES = 64         # room for the game's item list and the monster list


class Features(nn.Module):
    """Raw observation tensors to network inputs. No learned parameters."""

    def forward(self, obs, last_action, guided=True):
        """obs: dict of tensors with leading dimensions (..., ) and the
        block's shapes after them. last_action: (..., branches) integers."""
        s = obs["self"].float()
        scale = s.new_tensor([
            100, 100, 50, 200, 50, 50, 200, 50,         # health, armour, six kinds of ammunition
            10, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,        # weapon held, ten owned, ready
            300, 300, 300, 90, 1, 1, 1, 3, 1, 50, 12, 4,
            1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1])  # the ground about the feet, as it comes
        me = s / scale
        weapon = F.one_hot(s[..., 8].long().clamp(0, L.WEAPONS), L.WEAPONS + 1).float()

        g = obs["guide"].float()
        if not guided:
            g = torch.zeros_like(g)
        guide = torch.stack([g[..., 0], g[..., 1] / 180, g[..., 2] / 90,
                             torch.log1p(g[..., 3] / 64) / 4,
                             torch.sin(torch.deg2rad(g[..., 1])) * g[..., 0],
                             torch.cos(torch.deg2rad(g[..., 1])) * g[..., 0]], -1)

        dist = obs["ray_dist"].float()
        near = 1 / (1 + dist / 128)
        kind = F.one_hot(obs["ray_kind"].long().clamp(0, L.HIT_KINDS - 1), L.HIT_KINDS).float()
        rays = torch.cat([near.unsqueeze(-1), obs["ray_slope"].float().unsqueeze(-1), kind], -1)
        lead = rays.shape[:-2]
        rays = rays.reshape(*lead, L.RAYS_Y, L.RAYS_X, 2 + L.HIT_KINDS)

        e = obs["ents"].float()
        ent_kind = e[..., 0].long().clamp(0, L.KINDS - 1)
        ent_type = e[..., 1].long().clamp(0, ITEM_TYPES - 1)
        ent = torch.stack([e[..., 2] / 45, e[..., 3] / 30, 1 / (1 + e[..., 4] / 128),
                           e[..., 5] / 300, e[..., 6] / 300, e[..., 7] / 300,
                           e[..., 8], e[..., 9], e[..., 10] / 64, e[..., 11] / 64], -1)

        last = torch.cat([F.one_hot(last_action[..., i].long(), n).float()
                          for i, n in enumerate(L.ACT_SIZES)], -1)
        return {"flat": torch.cat([me, weapon, guide, last], -1), "rays": rays,
                "ent": ent, "ent_kind": ent_kind, "ent_type": ent_type}


FLAT = L.SELF_FLOATS + L.WEAPONS + 1 + 6 + sum(L.ACT_SIZES)


class Policy(nn.Module):
    def __init__(self, hidden=256, guided=True):
        super().__init__()
        self.guided = guided
        self.hidden = hidden
        self.features = Features()

        self.rays = nn.Sequential(
            nn.Conv2d(2 + L.HIT_KINDS, 32, 3, padding=1), nn.ReLU(),
            nn.Conv2d(32, 32, 3, stride=(1, 2), padding=1), nn.ReLU(),
            nn.Conv2d(32, 16, 3, stride=(2, 2), padding=1), nn.ReLU(),
            nn.Flatten())
        rays_out = 16 * 5 * 6

        self.kind_emb = nn.Embedding(L.KINDS, 8)
        self.type_emb = nn.Embedding(ITEM_TYPES, 8)
        self.ent = nn.Sequential(nn.Linear(10 + 16, 64), nn.ReLU(), nn.Linear(64, 64), nn.ReLU())

        self.mix = nn.Sequential(nn.Linear(FLAT + rays_out + 64, hidden), nn.ReLU())
        self.gru = nn.GRU(hidden, hidden, batch_first=True)
        self.heads = nn.ModuleList([nn.Linear(hidden, n) for n in L.ACT_SIZES])
        self.value = nn.Linear(hidden, 1)

    def encode(self, obs, last_action):
        """Inputs with leading dimensions (batch, time). Returns (batch, time, hidden)."""
        f = self.features(obs, last_action, self.guided)
        b, t = f["flat"].shape[:2]
        rays = self.rays(f["rays"].reshape(b * t, L.RAYS_Y, L.RAYS_X, -1).permute(0, 3, 1, 2))
        ent = torch.cat([f["ent"], self.kind_emb(f["ent_kind"]), self.type_emb(f["ent_type"])], -1)
        ent = self.ent(ent)
        # rows with nothing in them are left out of the maximum
        empty = (f["ent_kind"] == 0).unsqueeze(-1)
        ent = ent.masked_fill(empty, 0).amax(-2)
        return self.mix(torch.cat([f["flat"], rays.reshape(b, t, -1), ent], -1))

    def forward(self, obs, last_action, state=None, starts=None):
        """obs, last_action: leading dimensions (batch, time). state: the
        GRU's state (1, batch, hidden) or None for zeros. starts: (batch,
        time) of 1 where an episode begins, at which the state is cleared.

        Returns the logits of each branch, the value, and the new state."""
        x = self.encode(obs, last_action)
        b, t = x.shape[:2]
        if state is None:
            state = x.new_zeros(1, b, self.hidden)
        if starts is None or not bool(starts.any()):
            out, state = self.gru(x, state)
        else:
            outs = []
            for i in range(t):
                state = state * (1 - starts[:, i].float()).view(1, b, 1)
                o, state = self.gru(x[:, i:i + 1], state)
                outs.append(o)
            out = torch.cat(outs, 1)
        return [h(out) for h in self.heads], self.value(out).squeeze(-1), state

    @torch.no_grad()
    def act(self, obs, last_action, state, starts, sample=True):
        """One step for a batch of environments. obs and last_action have a
        leading dimension (batch,). Returns actions (batch, branches), their
        log probability, the value and the new state."""
        obs = {k: v.unsqueeze(1) for k, v in obs.items()}
        logits, value, state = self(obs, last_action.unsqueeze(1), state, starts.unsqueeze(1))
        actions, logp = [], 0
        for lg in logits:
            dist = torch.distributions.Categorical(logits=lg[:, 0])
            a = dist.sample() if sample else lg[:, 0].argmax(-1)
            actions.append(a)
            logp = logp + dist.log_prob(a)
        return torch.stack(actions, -1), logp, value[:, 0], state
