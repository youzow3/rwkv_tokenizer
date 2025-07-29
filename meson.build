project('RWKV Tokenizer', 'c')

pkgconfig = import('pkgconfig')

rwkv_tokenizer_inc = include_directories('.')
rwkv_tokenizer_lib = library('rwkv_tokenizer', 'rwkv_tokenizer.h', 'rwkv_tokenizer.c')
rwkv_tokenizer_dep = declare_dependency(include_directories: rwkv_tokenizer_inc, link_with: rwkv_tokenizer_lib)

pkgconfig.generate(rwkv_tokenizer_lib, description: 'RWKV Tokenizer library')
