Import('RTT_ROOT')
Import('rtconfig')
from building import *

cwd = GetCurrentDir()
src = Glob('core/*.c')

CPPPATH = [cwd + '/inc']

if GetDepend('ADB_SERVICE_SHELL_ENABLE'):
    src += ['services/shell_service.c']

if GetDepend('ADB_SERVICE_FILESYNC_ENABLE'):
    src += ['services/file_sync_service.c']

group = DefineGroup('adb', src, depend = ['PKG_USING_ADB'], CPPPATH = CPPPATH)

Return('group')
