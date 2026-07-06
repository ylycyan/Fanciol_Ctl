Import("env")

# Generate .hex file from .elf
env.AddPostAction(
    "$BUILD_DIR/${PROGNAME}.elf",
    env.VerboseAction(
        "$OBJCOPY -O ihex $BUILD_DIR/${PROGNAME}.elf $BUILD_DIR/${PROGNAME}.hex",
        "Building $BUILD_DIR/${PROGNAME}.hex"
    )
)
