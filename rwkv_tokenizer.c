
#include "rwkv_tokenizer.h"

static size_t
u8len (char *ch, size_t len)
{
  size_t estimate_size = 1;
  if ((ch[0] & 0xf8) == 0xf0)
    estimate_size = 4;
  if ((ch[0] & 0xf0) == 0xe0)
    estimate_size = 3;
  if ((ch[0] & 0xe0) == 0xc0)
    estimate_size = 2;
  if ((ch[0] & 0x80) == 0)
    return 1;

  for (size_t k = 1; k < estimate_size; k++)
    if ((ch[k] & 0xc0) != 0x80)
      return (size_t)-1;
  return estimate_size;
}

static size_t
u8slen (char *str)
{
  size_t alen = strlen (str);
  size_t ulen = 0;
  for (size_t k = 0; k < alen;)
    {
      size_t uclen = u8len (str + k, alen - k);
      if (uclen == (size_t)-1)
        return -1;
      k += uclen;
      ulen++;
    }

  return ulen;
}

static size_t
itou8 (uint32_t i, char *u8, size_t u8size)
{
  if (u8size == 0)
    return 0;

  if (i < 0x80)
    {
      u8[0] = i;
      return 1;
    }

  if (i < 0x800)
    {
      if (u8size < 2)
        return 0;
      u8[0] = 0xc0 | (i >> 6);
      u8[1] = 0x80 | (i & 0x3f);
      return 2;
    }

  if (i < 0x10000)
    {
      if (u8size < 3)
        return 0;
      u8[0] = 0xe0 | (i >> 12);
      u8[1] = 0x80 | ((i >> 6) & 0x3f);
      u8[2] = 0x80 | (i & 0x3f);
      return 3;
    }

  if (i < 0x110000)
    {
      if (u8size < 4)
        return 0;
      u8[0] = 0xf0 | (i >> 18);
      u8[1] = 0x80 | ((i >> 12) & 0x3f);
      u8[2] = 0x80 | ((i >> 6) & 0x3f);
      u8[3] = 0x80 | (i & 0x3f);
      return 4;
    }

  return 0;
}

static char *
python_str (char *str, size_t *py_str_len)
{
  char *squote = strchr (str, '\'');
  char *dquote = strchr (str, '\"');
  char quote = 0;
  char *quote_pos = NULL;
  if (squote == NULL)
    {
      quote = '\"';
      quote_pos = dquote;
    }
  else if (dquote == NULL)
    {
      quote = '\'';
      quote_pos = squote;
    }
  else if ((squote != NULL) && (dquote != NULL))
    {
      quote = (squote - dquote) < 0 ? '\'' : '\"';
      quote_pos = (squote - dquote) < 0 ? squote : dquote;
    }
  else
    return NULL;

  bool raw = false;
  bool unicode = false;
  bool format = false;
  bool bytes = false;
  for (size_t k = 0; k < (quote_pos - str); k++)
    {
      switch (tolower (str[k]))
        {
        case 'r':
          raw = true;
          break;
        case 'u':
          unicode = true;
          break;
        case 'f':
          format = true;
          break;
        case 'b':
          bytes = true;
          break;
        }
    }

  if (format)
    return NULL;

  size_t quote_pos_len = strlen (quote_pos);
  size_t encoded_str_size = quote_pos_len;
  char *encoded_str = malloc (encoded_str_size);
  if (encoded_str == NULL)
    return NULL;
  size_t encoded_str_pos = 0;
  bool escape = false;
  char escape_str[16];
  size_t escape_str_pos = 0;
  for (size_t k = 1; k < quote_pos_len;)
    {
      size_t ch_len = u8len (quote_pos + k, quote_pos_len - k);
      if (ch_len == -1)
        goto on_encode_error;

      if ((ch_len != 1) && escape)
        goto on_encode_error;

      if (escape && (escape_str_pos > 0))
        {
          switch (escape_str[0])
            {
            case '\\':
            case '\'':
            case '\"':
              encoded_str[encoded_str_pos++] = escape_str[0];
              escape = false;
              break;
            case 'a':
              encoded_str[encoded_str_pos++] = '\a';
              escape = false;
              break;
            case 'b':
              encoded_str[encoded_str_pos++] = '\b';
              escape = false;
              break;
            case 'f':
              encoded_str[encoded_str_pos++] = '\f';
              escape = false;
              break;
            case 'n':
              encoded_str[encoded_str_pos++] = '\n';
              escape = false;
              break;
            case 'r':
              encoded_str[encoded_str_pos++] = '\r';
              escape = false;
              break;
            case 't':
              encoded_str[encoded_str_pos++] = '\t';
              escape = false;
              break;
            case 'v':
              encoded_str[encoded_str_pos++] = '\v';
              escape = false;
              break;
            case 'u':
            case 'U':
              if (bytes)
                goto on_encode_error;
            case 'o':
            case 'x':
              escape = isxdigit (quote_pos[k]);
              break;
            case 'N':
              goto on_encode_error;
            }

          if (!escape && (escape_str[0] == 'o'))
            {
              int code = strtol (escape_str + 1, NULL, 8);
              if (bytes)
                encoded_str[encoded_str_pos++] = code;
              else
                {
                  size_t ul = itou8 (code, encoded_str,
                                     encoded_str_size - encoded_str_pos);
                  encoded_str_pos += ul;
                }
            }
          if (!escape && (escape_str[0] == 'x'))
            {
              int code = strtol (escape_str + 1, NULL, 16);
              if (bytes)
                encoded_str[encoded_str_pos++] = code;
              else
                {
                  size_t ul = itou8 (code, encoded_str,
                                     encoded_str_size - encoded_str_pos);
                  encoded_str_pos += ul;
                }
            }
          if (!escape && (escape_str[0] == 'u'))
            {
              if (strlen (escape_str + 1) != 4)
                goto on_encode_error;
              size_t l = itou8 (strtol (escape_str + 1, NULL, 16), encoded_str,
                                encoded_str_size - encoded_str_pos);
              if (l == 0)
                goto on_encode_error;
              encoded_str_pos += l;
            }
          if (!escape && (escape_str[0] == 'U'))
            {
              if (strlen (escape_str + 1) != 8)
                goto on_encode_error;
              size_t l = itou8 (strtol (escape_str + 1, NULL, 16), encoded_str,
                                encoded_str_size - encoded_str_pos);
              if (l == 0)
                goto on_encode_error;
              encoded_str_pos += l;
            }
        }

      if (!escape && (quote_pos[k] == quote))
        {
          encoded_str[encoded_str_pos] = 0;
          break;
        }

      if (!raw && !escape && (quote_pos[k] == '\\'))
        {
          escape = true;
          escape_str_pos = 0;
          memset (escape_str, 0, sizeof (escape_str));
          k += ch_len;
          continue;
        }

      if (!escape || raw)
        {
          memcpy (encoded_str + encoded_str_pos, quote_pos + k, ch_len);
          encoded_str_pos += ch_len;
          k += ch_len;
          continue;
        }

      escape_str[escape_str_pos++] = quote_pos[k++];
    }

  *py_str_len = encoded_str_pos;
  return encoded_str;
on_encode_error:
  free (encoded_str);
  return NULL;
}

bool
rwkv_tokenizer_init (RWKVTokenizer *tokenizer, char *filename)
{
  FILE *file = fopen (filename, "rt");
  if (file == NULL)
    return false;

  memset (&tokenizer->root, 0, sizeof (RWKVTokenizerEntry));
  tokenizer->root.ch = 0;
  tokenizer->root.id = -1;

  size_t vocab_buf_chunk = 65536;
  size_t vocab_buf_size = vocab_buf_chunk;
  size_t vocab_size = 0;
  tokenizer->token2text = malloc (sizeof (char *) * vocab_buf_chunk);
  if (tokenizer->token2text == NULL)
    goto on_error;
  memset (tokenizer->token2text, 0, sizeof (char *) * vocab_buf_chunk);

  char buf[512];
  while (fgets (buf, sizeof (buf), file))
    {

      long long vocab_id = -1;
      if (sscanf (buf, "%lld", &vocab_id) != 1)
        goto on_error;

      if (vocab_buf_size <= vocab_id)
        {
          void *rbuf
              = realloc (tokenizer->token2text,
                         sizeof (char *) * (vocab_buf_size + vocab_buf_chunk));
          if (rbuf == NULL)
            goto on_error;
          memset (rbuf + vocab_buf_size, 0, sizeof (char *) * vocab_buf_chunk);
          tokenizer->token2text = rbuf;
          vocab_buf_size += vocab_buf_chunk;
        }

      char *text_len_str = strrchr (buf, ' ');
      if (text_len_str == NULL)
        goto on_error;

      size_t text_len = 0;
      if (sscanf (text_len_str, "%zd", &text_len) != 1)
        goto on_error;

      char *text_start = strchr (buf, ' ');
      if (text_start == NULL)
        goto on_error;
      text_start += 1;
      size_t text_repr_size = (size_t)(text_len_str - text_start);
      char text_repr[text_repr_size + 1];
      strncpy (text_repr, text_start, text_repr_size);

      size_t py_str_len = 0;
      char *text = python_str (text_repr, &py_str_len);
      if (text == NULL)
        goto on_error;

      if (py_str_len != text_len)
        {
          free (text);
          goto on_error;
        }

      RWKVTokenizerEntry *entry = &tokenizer->root;
      for (size_t k = 0; k < strlen (text); k++)
        {
          if (entry->next[(unsigned char)text[k]] != NULL)
            {
              entry = entry->next[(unsigned char)text[k]];
              continue;
            }

          entry->next[(unsigned char)text[k]]
              = malloc (sizeof (RWKVTokenizerEntry));
          if (entry->next[(unsigned char)text[k]] == NULL)
            {
              free (text);
              goto on_error;
            }
          memset (entry->next[(unsigned char)text[k]], 0,
                  sizeof (RWKVTokenizerEntry));

          entry->next[(unsigned char)text[k]]->prev = entry;
          entry = entry->next[(unsigned char)text[k]];
          entry->id = -1;
          entry->ch = text[k];
        }

      entry->id = vocab_id;
      tokenizer->token2text[vocab_id] = text;

      if (vocab_size < vocab_id)
        vocab_size = vocab_id;
    }

  tokenizer->vocab_size = vocab_size + 1;

  fclose (file);
  return true;
on_error:
  fclose (file);
  return false;
}

void
rwkv_tokenizer_free (RWKVTokenizer *tokenizer)
{
  RWKVTokenizer empty_tokenizer = { 0 };
  if (!memcmp (tokenizer, &empty_tokenizer, sizeof (RWKVTokenizer)))
    return;

  size_t idx = -1;
  for (RWKVTokenizerEntry *entry = &tokenizer->root; entry != NULL;)
    {
      size_t search_idx = -1;
      for (size_t k = 0; k < 256; k++)
        {
          if (entry->next[k] == NULL)
            continue;
          entry = entry->next[k];
          idx = k;
          search_idx = k;
          break;
        }

      if ((search_idx == -1) && (idx != -1))
        {
          RWKVTokenizerEntry *_entry = entry->prev;
          if (entry == &tokenizer->root)
            break;
          _entry->next[idx] = NULL;
          free (entry);
          entry = _entry;
          idx = -1;
        }
      else if ((search_idx == -1) && (idx == -1))
        entry = entry->prev;
    }

  for (size_t k = 0; k < tokenizer->vocab_size; k++)
    {
      if (tokenizer->token2text[k] != NULL)
        free (tokenizer->token2text[k]);
    }
  free (tokenizer->token2text);
}

int64_t *
rwkv_tokenizer_tokenize (RWKVTokenizer *tokenizer, char *text,
                         size_t *tokens_len)
{
  int64_t *tokens;
  size_t _tokens_len = 0;
  size_t tokens_size = 512;
  size_t tokens_size_chunk = 512;

  tokens = malloc (sizeof (int64_t) * tokens_size_chunk);
  if (tokens == NULL)
    goto on_error;

  RWKVTokenizerEntry *entry = &tokenizer->root;
  for (size_t k = 0; k < strlen (text); k++)
    {
      if (entry->next[(unsigned char)text[k]] != NULL)
        {
          entry = entry->next[(unsigned char)text[k]];
          continue;
        }

      while (entry->id == -1)
        {
          entry = entry->prev;
          k--;
        }

      if (_tokens_len == tokens_size)
        {
          void *buf = realloc (
              tokens, sizeof (int64_t) * (_tokens_len + tokens_size_chunk));
          if (buf == NULL)
            goto on_error;
          tokens = buf;
        }

      tokens[_tokens_len++] = entry->id;
      entry = &tokenizer->root;
      k--;
    }

  if (entry->id != -1)
    {
      if (_tokens_len == tokens_size)
        {
          void *buf = realloc (tokens, sizeof (int64_t) * (_tokens_len + 1));
          if (buf == NULL)
            goto on_error;
          tokens = buf;
        }

      tokens[_tokens_len++] = entry->id;
    }

  *tokens_len = _tokens_len;
  return tokens;

on_error:
  free (tokens);
  return NULL;
}

char *
rwkv_tokenizer_detokenize (RWKVTokenizer *tokenizer, int64_t *tokens,
                           size_t tokens_len)
{
  const size_t text_size_chunk = tokens_len * 8;
  size_t text_size = text_size_chunk;
  size_t text_len = 0;
  char *text = malloc (text_size);
  if (text == NULL)
    return NULL;

  for (size_t k = 0; k < tokens_len; k++)
    {
      const char *token_text = tokenizer->token2text[tokens[k]];
      if (token_text == NULL)
        token_text = " ";
      size_t token_text_len = strlen (token_text);
      if ((text_size - text_len) <= token_text_len)
        {
          void *buf = realloc (text, text_size + text_size_chunk);
          if (buf == NULL)
            goto on_error;
          text = buf;
          text_size += text_size_chunk;
        }

      strcpy (text + text_len, token_text);
      text_len += token_text_len;
    }

  return text;

on_error:
  free (text);
  return NULL;
}
