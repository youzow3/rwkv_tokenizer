
#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct _RWKVTokenizerEntry RWKVTokenizerEntry;
typedef struct _RWKVTokenizer RWKVTokenizer;

struct _RWKVTokenizerEntry
{
  RWKVTokenizerEntry *next[256];
  RWKVTokenizerEntry *prev;
  int64_t id;
  int64_t ch; // char ch;
};

struct _RWKVTokenizer
{
  RWKVTokenizerEntry root;
  char **token2text;
  size_t vocab_size;
};

#ifdef __cplusplus
extern "C" {
#endif

bool rwkv_tokenizer_init (RWKVTokenizer *tokenizer, char *filename);
void rwkv_tokenizer_free (RWKVTokenizer *tokenizer);
int64_t *rwkv_tokenizer_tokenize (RWKVTokenizer *tokenizer, char *text,
                                  size_t *tokens_len);
char *rwkv_tokenizer_detokenize (RWKVTokenizer *tokenizer, int64_t *tokens,
                                 size_t tokens_len);

#ifdef __cplusplus
}
#endif
