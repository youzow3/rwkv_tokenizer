project('RWKV Tokenizer', 'c')

pkgconfig = import('pkgconfig')

rwkv_tokenizer = library('rwkv_tokenizer', 'rwkv_tokenizer.h', 'rwkv_tokenizer.c')

pkgconfig.generate(rwkv_tokenizer, description: 'RWKV Tokenizer library')
