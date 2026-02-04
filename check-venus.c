/*
 * check-venus.c
 * Small utility to check whether the physical machine supports the
 * host-side capabilities Venus requires (Vulkan extensions + host features).
 *
 * Build: make check-venus
 * Run:   ./check-venus
 */

/* Make sure GNU extensions (like syscall prototypes) are exposed */
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dlfcn.h>
#include <errno.h>
#include <sys/syscall.h>

#ifdef __linux__
#include <linux/memfd.h>
#endif

#include <vulkan/vulkan.h>

static const char *required_exts[] = {
    "VK_KHR_get_physical_device_properties2",
    "VK_KHR_get_memory_requirements2",
    "VK_KHR_external_memory",
    "VK_KHR_external_memory_fd",
    "VK_EXT_external_memory_dma_buf",
    "VK_KHR_external_fence_fd",
    "VK_KHR_external_semaphore_fd",
    "VK_KHR_dedicated_allocation",
};
static const size_t required_exts_count = sizeof(required_exts) / sizeof(required_exts[0]);

static bool
has_ext(const char *name, const VkExtensionProperties *exts, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        if (strcmp(name, exts[i].extensionName) == 0)
            return true;
    }
    return false;
}

static bool
check_memfd_create(void)
{
#ifdef SYS_memfd_create
    int fd = syscall(SYS_memfd_create, "chk", MFD_CLOEXEC);
    if (fd >= 0) {
        close(fd);
        return true;
    }
    return false;
#else
    (void)0; /* Not supported at compile-time */
    return false;
#endif
}

static bool
check_udmabuf(void)
{
    int fd = open("/dev/udmabuf", O_RDONLY | FD_CLOEXEC);
    if (fd >= 0) {
        close(fd);
        return true;
    }
    return false;
}

static bool
check_gbm(void)
{
    const char *libs[] = {"libgbm.so.1", "libgbm.so"};
    for (size_t i = 0; i < sizeof(libs)/sizeof(libs[0]); i++) {
        void *h = dlopen(libs[i], RTLD_NOW | RTLD_LOCAL);
        if (h) {
            dlclose(h);
            return true;
        }
    }
    return false;
}

int main(void)
{
    VkResult res;

    uint32_t instance_version = VK_API_VERSION_1_0;
    PFN_vkEnumerateInstanceVersion pEnumerateInstanceVersion =
        (PFN_vkEnumerateInstanceVersion)vkGetInstanceProcAddr(NULL, "vkEnumerateInstanceVersion");
    if (pEnumerateInstanceVersion) {
        pEnumerateInstanceVersion(&instance_version);
    }

    printf("Vulkan loader instance version: %u.%u.%u\n",
           VK_VERSION_MAJOR(instance_version),
           VK_VERSION_MINOR(instance_version),
           VK_VERSION_PATCH(instance_version));

    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = NULL,
        .pApplicationName = "check-venus",
        .applicationVersion = 1,
        .pEngineName = "none",
        .engineVersion = 1,
        .apiVersion = instance_version >= VK_MAKE_VERSION(1,1,0) ? VK_API_VERSION_1_1 : VK_API_VERSION_1_0,
    };

    VkInstanceCreateInfo inst_info = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = NULL,
        .flags = 0,
        .pApplicationInfo = &app_info,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = NULL,
        .enabledExtensionCount = 0,
        .ppEnabledExtensionNames = NULL,
    };

    VkInstance instance;
    res = vkCreateInstance(&inst_info, NULL, &instance);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "vkCreateInstance failed: %d\n", res);
        return 2;
    }

    uint32_t gpu_count = 0;
    res = vkEnumeratePhysicalDevices(instance, &gpu_count, NULL);
    if (res != VK_SUCCESS || gpu_count == 0) {
        fprintf(stderr, "vkEnumeratePhysicalDevices failed or no GPUs: %d\n", res);
        vkDestroyInstance(instance, NULL);
        return 3;
    }

    VkPhysicalDevice *gpus = calloc(gpu_count, sizeof(VkPhysicalDevice));
    if (!gpus) {
        perror("calloc");
        vkDestroyInstance(instance, NULL);
        return 4;
    }

    res = vkEnumeratePhysicalDevices(instance, &gpu_count, gpus);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "vkEnumeratePhysicalDevices fetch failed: %d\n", res);
        free(gpus);
        vkDestroyInstance(instance, NULL);
        return 5;
    }

    bool any_gpu_ok = false;

    for (uint32_t i = 0; i < gpu_count; i++) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(gpus[i], &props);
        printf("\nPhysical device %u: %s (apiVersion %u.%u.%u)\n",
               i, props.deviceName,
               VK_VERSION_MAJOR(props.apiVersion),
               VK_VERSION_MINOR(props.apiVersion),
               VK_VERSION_PATCH(props.apiVersion));

        uint32_t ext_count = 0;
        res = vkEnumerateDeviceExtensionProperties(gpus[i], NULL, &ext_count, NULL);
        if (res != VK_SUCCESS) {
            fprintf(stderr, "vkEnumerateDeviceExtensionProperties failed: %d\n", res);
            continue;
        }

        VkExtensionProperties *exts = calloc(ext_count, sizeof(VkExtensionProperties));
        if (!exts) {
            perror("calloc");
            continue;
        }

        res = vkEnumerateDeviceExtensionProperties(gpus[i], NULL, &ext_count, exts);
        if (res != VK_SUCCESS) {
            fprintf(stderr, "vkEnumerateDeviceExtensionProperties fetch failed: %d\n", res);
            free(exts);
            continue;
        }

        bool missing_any = false;
        for (size_t j = 0; j < required_exts_count; j++) {
            const char *req = required_exts[j];
            bool ok = has_ext(req, exts, ext_count);
            /* treat some KHR extensions as satisfied if device supports Vulkan 1.1 */
            if (!ok && (strcmp(req, "VK_KHR_get_physical_device_properties2") == 0 ||
                        strcmp(req, "VK_KHR_get_memory_requirements2") == 0)) {
                if (props.apiVersion >= VK_MAKE_VERSION(1,1,0))
                    ok = true;
            }

            printf("  %s: %s\n", req, ok ? "OK" : "MISSING");
            if (!ok)
                missing_any = true;
        }

        if (!missing_any) {
            any_gpu_ok = true;
            printf("=> This device appears to support all required Vulkan extensions.\n");
        } else {
            printf("=> This device is missing required extension(s).\n");
        }

        free(exts);
    }

    /* Host-side checks */
    printf("\nHost capability checks:\n");
    bool memfd_ok = check_memfd_create();
    printf("  memfd_create(): %s\n", memfd_ok ? "OK" : "MISSING");

    bool udmabuf_ok = check_udmabuf();
    printf("  /dev/udmabuf: %s\n", udmabuf_ok ? "present" : "not present");

    bool gbm_ok = check_gbm();
    printf("  libgbm: %s\n", gbm_ok ? "available" : "not found");

    free(gpus);
    vkDestroyInstance(instance, NULL);

    int exit_code = 0;
    if (!any_gpu_ok) exit_code = 10;
    if (!memfd_ok) exit_code = 11;
    if (!udmabuf_ok) exit_code = exit_code ? exit_code : 12;
    if (!gbm_ok) exit_code = exit_code ? exit_code : 13;

    if (exit_code == 0)
        printf("\nRESULT: Host seems to satisfy Venus requirements.\n");
    else
        printf("\nRESULT: Missing requirements. exit=%d\n", exit_code);

    return exit_code;
}
