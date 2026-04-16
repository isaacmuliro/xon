{
  "targets": [
    {
      "target_name": "xon",
      "sources": [ 
        "bindings/xon_node.cpp", 
        "src/xon_api.c", 
        "src/lexer.c",
        "src/logger.c"
      ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")",
        "include",
        "build"
      ],
      "defines": [ "NAPI_DISABLE_CPP_EXCEPTIONS" ],
      "cflags!": [ "-fno-exceptions" ],
      "cflags_cc!": [ "-fno-exceptions" ],
      "xcode_settings": {
        "GCC_ENABLE_CPP_EXCEPTIONS": "YES",
        "CLANG_CXX_LIBRARY": "libc++",
        "MACOSX_DEPLOYMENT_TARGET": "10.7"
      },
      "msvs_settings": {
        "VCCLCompilerTool": { "ExceptionHandling": 1 }
      }
    }
  ]
}
