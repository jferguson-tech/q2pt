# Wraps glslangValidator's list of SPIR-V words in braces, which makes it the
# initializer that glslc -mfmt=c writes.
file(READ ${IN} words)
file(WRITE ${OUT} "{\n${words}}\n")
