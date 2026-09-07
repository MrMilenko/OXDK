# Set up a shell for OXDK work.
#
# Source it:      . scripts/oxdk-env.sh [xbox|xbox360]
# Or use the launcher, which sources this for you:   ./oxdk [xbox|xbox360]
#
# Everything here is discovered, not hardcoded. common/toolchain.mk does the
# searching and this asks it, so a build and a shell always agree.

oxdk_env_root=$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")/.." 2>/dev/null && pwd)
if [ -z "$oxdk_env_root" ] || [ ! -f "$oxdk_env_root/common/toolchain.mk" ]; then
    oxdk_env_root=$(pwd)
fi

if [ ! -f "$oxdk_env_root/common/toolchain.mk" ]; then
    echo "oxdk-env: run this from the OXDK directory, or source it by its full path."
    return 1 2>/dev/null || exit 1
fi

OXDK_DIR=$oxdk_env_root
export OXDK_DIR

case "${1:-}" in
    xbox|"")        OXDK_TARGET=xbox ;;
    xbox360|360|xenon) OXDK_TARGET=xbox360 ;;
    *) echo "oxdk-env: target must be xbox or xbox360, not \"$1\""
       return 1 2>/dev/null || exit 1 ;;
esac
export OXDK_TARGET

# Ask the makefile what it would use, so there is one search and one answer.
oxdk_env_vars=$(make -f - <<MK 2>/dev/null
OXDK_ROOT := $OXDK_DIR
include $OXDK_DIR/common/toolchain.mk
all:
	@echo "OXDK_LLVM=\$(OXDK_LLVM)"
	@echo "OXDK_LIBCXX_DIR=\$(OXDK_LIBCXX_DIR)"
	@echo "LLD_LINK=\$(LLD_LINK)"
	@echo "LLD=\$(LLD)"
	@echo "OBJCOPY=\$(OBJCOPY)"
	@echo "LLVM_AR=\$(LLVM_AR)"
	@echo "LLVM_NM=\$(LLVM_NM)"
	@echo "OXDK_XBOX_XDK=\$(OXDK_XBOX_XDK)"
	@echo "OXDK_XBOX360_XDK=\$(OXDK_XBOX360_XDK)"
MK
)

for oxdk_env_line in $oxdk_env_vars; do
    case "$oxdk_env_line" in
        *=) : ;;                       # empty, leave it unset
        *=*) eval "export $oxdk_env_line" ;;
    esac
done

# The XDK for the chosen console.
if [ "$OXDK_TARGET" = xbox360 ]; then
    [ -n "$OXDK_XBOX360_XDK" ] && export XDK_DIR="$OXDK_XBOX360_XDK"
else
    [ -n "$OXDK_XBOX_XDK" ] && export XDK_DIR="$OXDK_XBOX_XDK"
fi

# The tools that get run by hand rather than by make.
oxdk_env_path=$OXDK_DIR/scripts
for oxdk_env_d in \
    "$OXDK_DIR/xbox/tools/cxbe" \
    "$OXDK_DIR/xbox360/tools/cxex" \
    "$OXDK_DIR/xbox360/tools/xbdm" \
    "$OXDK_DIR/xbox360/tools/xexutil" \
    "$OXDK_LLVM/bin"
do
    [ -d "$oxdk_env_d" ] && oxdk_env_path=$oxdk_env_path:$oxdk_env_d
done
case ":$PATH:" in
    *":$OXDK_DIR/scripts:"*) : ;;
    *) PATH=$oxdk_env_path:$PATH; export PATH ;;
esac

unset oxdk_env_root oxdk_env_vars oxdk_env_line oxdk_env_path oxdk_env_d

echo "OXDK ready. Target: $OXDK_TARGET"
if [ -z "$XDK_DIR" ]; then
    echo "No XDK found for $OXDK_TARGET. Set XDK_DIR, or run doctor.sh."
fi
