{
  'variables': {
    'asan%': 0,
    'werror': '',                    # Turn off -Werror in V8 build.
    'visibility%': 'hidden',         # V8's visibility setting
    'target_arch%': 'ia32',          # set v8's target architecture
    'host_arch%': 'ia32',            # set v8's host architecture
    'want_separate_host_toolset': 0, # V8 should not build target and host
    'library%': 'static_library',    # allow override to 'shared_library' for DLL/.so builds
    'component%': 'static_library',  # NB. these names match with what V8 expects
    'msvs_multi_core_compile': '0',  # we do enable multicore compiles, but not using the V8 way
    'python%': 'python', 
    
    'node_tag%': '',
    'uv_library%': 'static_library',
    
    # Default to -O0 for debug builds.
    'v8_optimized_debug%': 0,

    # Enable disassembler for `--print-code` v8 options
    'v8_enable_disassembler': 1,

    # Don't bake anything extra into the snapshot.
    'v8_use_external_startup_data%': 0,

    'conditions': [
      ['OS == "win"', {
        'os_posix': 0,
        'v8_postmortem_support%': 'false',
      }, {
        'os_posix': 1,
        'v8_postmortem_support%': 'true',
      }],
      ['GENERATOR == "ninja" or OS== "mac"', {
        'OBJ_DIR': '<(PRODUCT_DIR)/obj',
        'V8_BASE': '<(PRODUCT_DIR)/libv8_base.a',
      }, {
        'OBJ_DIR': '<(PRODUCT_DIR)/obj.target',
        'V8_BASE': '<(PRODUCT_DIR)/obj.target/deps/v8/tools/gyp/libv8_base.a',
      }],
      #['openssl_fips != ""', {
      #  'OPENSSL_PRODUCT': 'libcrypto.a',
      #}, {
      #  'OPENSSL_PRODUCT': 'libopenssl.a',
      #}],
      ['OS=="mac"', {
        'clang%': 1,
      }, {
        'clang%': 0,
      }],
    ],
  },

  'target_defaults': {
    'default_configuration': 'Release',
    'configurations': {
      'Debug': {
        'variables': {
          'v8_enable_handle_zapping': 1,
        },
        'defines': [ 'DEBUG', '_DEBUG' ],
        'cflags': [ '-g', '-O0' ],
        'conditions': [
          ['target_arch=="x64"', {
            'msvs_configuration_platform': 'x64',
          }],
        ],
        'msvs_settings': {
          'VCCLCompilerTool': {
            'RuntimeLibrary': 1, # static debug
            'Optimization': 0, # /Od, no optimization
            'MinimalRebuild': 'false',
            'OmitFramePointers': 'false',
            'BasicRuntimeChecks': 3, # /RTC1
          },
          'VCLinkerTool': {
            'LinkIncremental': 2, # enable incremental linking
          },
        },
      },
      'Release': {
        'variables': {
          'v8_enable_handle_zapping': 0,
        },
        'cflags': [ '-O3', '-ffunction-sections', '-fdata-sections' ],
        'conditions': [
          ['target_arch=="x64"', {
            'msvs_configuration_platform': 'x64',
          }],
          ['node_unsafe_optimizations==1', {
            'cflags': [ '-O3', '-ffunction-sections', '-fdata-sections' ],
            'ldflags': [ '-Wl,--gc-sections' ],
          }, {
            'cflags': [ '-O2', '-fno-strict-aliasing' ],
            'cflags!': [ '-O3', '-fstrict-aliasing' ],
            'conditions': [
              # Required by the dtrace post-processor. Unfortunately,
              # some gcc/binutils combos generate bad code when
              # -ffunction-sections is enabled. Let's hope for the best.
              ['OS=="solaris"', {
                # pull in V8's postmortem metadata
                'ldflags': [ '-Wl,-z,allextract' ],
                'cflags': [ '-ffunction-sections', '-fdata-sections' ],
              }, {
                'cflags!': [ '-ffunction-sections', '-fdata-sections' ],
              }],
              ['clang == 0 and gcc_version >= 40', {
                'cflags': [ '-fno-tree-vrp' ],
              }],
              ['clang == 0 and gcc_version <= 44', {
                'cflags': [ '-fno-tree-sink' ],
              }],
            ],
          }],
          ['OS=="solaris"', {
            'cflags': [ '-fno-omit-frame-pointer' ],
            # pull in V8's postmortem metadata
            'ldflags': [ '-Wl,-z,allextract' ]
          }],
        ],
        'msvs_settings': {
          'VCCLCompilerTool': {
            'RuntimeLibrary': 0, # static release
            'Optimization': 3, # /Ox, full optimization
            'FavorSizeOrSpeed': 1, # /Ot, favour speed over size
            'InlineFunctionExpansion': 2, # /Ob2, inline anything eligible
            'WholeProgramOptimization': 'true', # /GL, whole program optimization, needed for LTCG
            'OmitFramePointers': 'true',
            'EnableFunctionLevelLinking': 'true',
            'EnableIntrinsicFunctions': 'true',
            'RuntimeTypeInfo': 'false',
            'ExceptionHandling': '0',
            'AdditionalOptions': [
              '/MP', # compile across multiple CPUs
            ],
          },
          'VCLibrarianTool': {
            'AdditionalOptions': [
              '/LTCG', # link time code generation
            ],
          },
          'VCLinkerTool': {
            'LinkTimeCodeGeneration': 1, # link-time code generation
            'OptimizeReferences': 2, # /OPT:REF
            'EnableCOMDATFolding': 2, # /OPT:ICF
            'LinkIncremental': 1, # disable incremental linking
          },
        },
      }
    },
    'msvs_settings': {
      'VCCLCompilerTool': {
        'StringPooling': 'true', # pool string literals
        'DebugInformationFormat': 3, # Generate a PDB
        'WarningLevel': 3,
        'BufferSecurityCheck': 'true',
        'ExceptionHandling': 1, # /EHsc
        'SuppressStartupBanner': 'true',
        'WarnAsError': 'false',
      },
      'VCLibrarianTool': {
      },
      'VCLinkerTool': {
        'conditions': [
          ['target_arch=="x64"', {
            'TargetMachine' : 17 # /MACHINE:X64
          }],
        ],
        'GenerateDebugInformation': 'true',
        'RandomizedBaseAddress': 2, # enable ASLR
        'DataExecutionPrevention': 2, # enable DEP
        'AllowIsolation': 'true',
        'SuppressStartupBanner': 'true',
        'target_conditions': [
          ['_type=="executable"', {
            'SubSystem': 1, # console executable
          }],
        ],
      },
    },
    'conditions': [
      ['OS == "win"', {
        'msvs_cygwin_shell': 0, # prevent actions from trying to use cygwin
        'defines': [
          'WIN32',
          # we don't really want VC++ warning us about
          # how dangerous C functions are...
          '_CRT_SECURE_NO_DEPRECATE',
          # ... or that C implementations shouldn't use
          # POSIX names
          '_CRT_NONSTDC_NO_DEPRECATE',
          'BUILDING_V8_SHARED=1',
          'BUILDING_UV_SHARED=1',
        ],
        'conditions': [
          ['target_arch=="ia32"', {'defines': ['IA32']}],
          ['target_arch=="x64"', {'defines': ['X64']}]
        ],
      }, {
        'defines': [
          '_LARGEFILE_SOURCE',
          '_FILE_OFFSET_BITS=64',
        ],
      }],
      [ 'OS in "linux freebsd openbsd solaris"', {
        'cflags': [ '-pthread', ],
        'ldflags': [ '-pthread' ],
      }],      
      [ 'OS=="android" or OS=="linux" or OS=="freebsd" or OS=="openbsd" or OS=="solaris"', {
        'cflags': [ '-Wall' ],
        'cflags_cc': [ '-frtti', '-fexceptions', '-std=gnu++11' ],
        'ldflags': [ '-rdynamic' ],
        'conditions': [
          [ 'target_arch=="ia32"', {
            'cflags': [ '-m32', '-DIA32' ],
            'ldflags': [ '-m32', '-DIA32' ],
          }],
          [ 'target_arch=="x64"', {
            'cflags': [ '-m64', '-DX64' ],
            'ldflags': [ '-m64', '-DX64' ],
          }],
          [ 'OS=="solaris"', {
            'cflags': [ '-pthreads' ],
            'ldflags': [ '-pthreads' ],
            'cflags!': [ '-pthread' ],
            'ldflags!': [ '-pthread' ],
          }],
        ],
      }],
      [ 'OS=="android"', {
        'defines': ['_GLIBCXX_USE_C99_MATH', 'ANDROID'],
        'libraries': [ '-llog' ],
      }],
      ['OS=="mac"', {
        'defines': ['_DARWIN_USE_64_BIT_INODE=1'],
        'xcode_settings': {
          'ALWAYS_SEARCH_USER_PATHS': 'NO',
          'GCC_CW_ASM_SYNTAX': 'NO',               # No -fasm-blocks
          'GCC_DYNAMIC_NO_PIC': 'NO',              # No -mdynamic-no-pic
                                                   # (Equivalent to -fPIC)
          'GCC_ENABLE_CPP_EXCEPTIONS': 'YES',      # -fno-exceptions
          'GCC_ENABLE_CPP_RTTI': 'YES',            # -fno-rtti
          'GCC_ENABLE_PASCAL_STRINGS': 'NO',       # No -mpascal-strings
          'GCC_THREADSAFE_STATICS': 'NO',          # -fno-threadsafe-statics
          'GCC_VERSION': '4.2',
          'PREBINDING': 'NO',                      # No -Wl,-prebind
          'MACOSX_DEPLOYMENT_TARGET': '10.10',     # -mmacosx-version-min=10.10
          'USE_HEADERMAP': 'NO',
          'OTHER_CFLAGS': [
            '-fno-strict-aliasing',
            '-stdlib=libc++',
          ],
          'WARNING_CFLAGS': [
            '-Wall',
            '-Wendif-labels',
            '-W',
            '-Wno-unused-parameter',
          ],
        },
        'target_conditions': [
          ['_type!="static_library"', {
            'xcode_settings': {'OTHER_LDFLAGS': ['-Wl,-search_paths_first']},
          }],
        ],
        'conditions': [
          ['target_arch=="ia32"', {
            'xcode_settings': {'ARCHS': ['i386'], 'OTHER_CFLAGS': ['-DIA32']},
          }],
          ['target_arch=="x64"', {
            'xcode_settings': {'ARCHS': ['x86_64'], 'OTHER_CFLAGS': ['-DX64']},
          }],
          ['clang==1', {
            'xcode_settings': {
              'GCC_VERSION': 'com.apple.compilers.llvm.clang.1_0',
              'CLANG_CXX_LANGUAGE_STANDARD': 'gnu++11',  # -std=gnu++11
              'CLANG_CXX_LIBRARY': 'libc++',
            },
          }],
        ],
      }],
    ],
  }
}
