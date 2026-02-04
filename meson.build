project('check-venus', 'c')

vulkan_dep = dependency('vulkan', required: false)
cc = meson.get_compiler('c')

if vulkan_dep.found()
  dl_lib = cc.find_library('dl', required: false)

  executable('check-venus', 'check-venus.c',
             dependencies: [vulkan_dep, dl_lib],
             install: true)
else
  warning('Vulkan dependency not found; not building check-venus')
endif
