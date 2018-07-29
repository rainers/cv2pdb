# This makefile follows grammar syntax of 'MiniBuild' build system,
# for details see https://minibuild.github.io/minibuild/

#pragma build output='bin/output'
#pragma os:windows default model=x86_64:win64,x86:win32
#pragma os:windows toolset module=msvs version=latest alias=x86:win32,x86_64:win64

module_type = 'executable'
module_name = 'cv2pdb'

src_search_dir_list = ['src']
asm_search_dir_list = ['src']

build_list = [
  'cvutil.cpp',
  'cv2pdb.cpp',
  'demangle.cpp',
  'dwarflines.cpp',
  'dwarf2pdb.cpp',
  'main.cpp',
  'mspdb.cpp',
  'PEImage.cpp',
  'readDwarf.cpp',
  'symutil.cpp',
]

build_list_windows_x86_64 = ['cvt80to64.asm']

prebuilt_lib_list_windows = ['advapi32']

definitions = ['UNICODE', '_CRT_SECURE_NO_WARNINGS']
