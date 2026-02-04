#!/usr/bin/env bash
set -euo pipefail

REQUIRED_EXTS=(
  "VK_KHR_get_physical_device_properties2"
  "VK_KHR_get_memory_requirements2"
  "VK_KHR_external_memory"
  "VK_KHR_external_memory_fd"
  "VK_EXT_external_memory_dma_buf"
  "VK_KHR_external_fence_fd"
  "VK_KHR_external_semaphore_fd"
  "VK_KHR_dedicated_allocation"
)

echo "Checking for vulkaninfo..."
if ! command -v vulkaninfo >/dev/null 2>&1; then
  echo "ERROR: vulkaninfo not found in PATH. Install Vulkan SDK or package providing vulkaninfo."
  exit 2
fi

# Run vulkaninfo once and capture its output into a shell variable
# Capture exit status separately to avoid subtle set -e behavior with command substitution
echo "Running vulkaninfo (this may take a moment) and capturing output in memory"
VULKANINFO_OUTPUT="$(vulkaninfo 2>&1)"
VULKANINFO_STATUS=$?
if [ "$VULKANINFO_STATUS" -ne 0 ]; then
  echo "Note: vulkaninfo exited with status $VULKANINFO_STATUS; captured output may be incomplete."
fi
# Fail early if there was no output to search
if [ -z "$VULKANINFO_OUTPUT" ]; then
  echo "ERROR: vulkaninfo produced no output; cannot detect extensions."
  exit 2
fi

echo; echo "=== Vulkan extension checks ==="
MISSING=()
for ext in "${REQUIRED_EXTS[@]}"; do
  if [[ "$VULKANINFO_OUTPUT" == *"$ext"* ]]; then
    printf "  %s: OK\n" "$ext"
  else
    printf "  %s: MISSING\n" "$ext"
    MISSING+=("$ext")
  fi
done

echo; echo "=== Host capability checks ==="
# memfd_create test
cat > /tmp/check_memfd.c <<'C' || exit 1
#define _GNU_SOURCE
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/memfd.h>
#include <stdio.h>
#include <errno.h>
int main(void){
  int fd = syscall(SYS_memfd_create, "chk", MFD_CLOEXEC);
  if (fd < 0) { perror("memfd_create"); return 1; }
  return 0;
}
C
if gcc /tmp/check_memfd.c -o /tmp/check_memfd >/dev/null 2>&1 && /tmp/check_memfd >/dev/null 2>&1; then
  echo "  memfd_create: OK"
else
  echo "  memfd_create: FAIL"
fi
rm -f /tmp/check_memfd /tmp/check_memfd.c

# udmabuf device
if [ -e /dev/udmabuf ]; then
  echo "  /dev/udmabuf: present"
else
  echo "  /dev/udmabuf: not present"
fi

# gbm presence
if pkg-config --exists gbm 2>/dev/null; then
  echo "  libgbm (pkg-config): available"
else
  echo "  libgbm (pkg-config): not found"
fi

echo
if [ ${#MISSING[@]} -eq 0 ]; then
  echo "RESULT: All required Vulkan extensions found (at least somewhere in vulkaninfo output)."
else
  echo "RESULT: Missing extensions:" 
  for e in "${MISSING[@]}"; do echo "  - $e"; done
fi

exit 0
