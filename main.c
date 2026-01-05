/*
 * Author - William Green
 */
#include <stdio.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <curl/curl.h>
#include <cjson/cJSON.h>

#define ARRAY_MAX(x) (sizeof(x) / sizeof(x[0]))

enum {
  TYPE_STR,
  TYPE_DOUBLE
};

enum {
  TOK_COIN,
  TOK_FIAT,
  TOK_HOLDINGS
};

enum {
  GBP,
  USD,
  EUR
};

enum {
  BTC,
  XMR,
  LTC,
  ETH,
  DOGE,
  BCH, 
  USDT,
  WOW /* coins like wownero which are not listed on coingecko may be inaccurate / not work */
};

struct token_map {
  char *cmd;
  int tok_type;
  int val_type;
};

struct coin_map {
  char *symbol;
  char *full;
  int enum_val;
  char *url;
};

struct fiat_map {
  char *name;
  char *symbol;
  int enum_val;
};

struct page_data {
  char *buf;
  size_t len;
};

static int help(const char *);
static char *get_page(const char *);
size_t write_page_data(void *, size_t, size_t, void *);
static char *append_str(char *, char *);
static double json_get_price(int, int, const char *);
static void display_price(int, int, double);
static void display_holdings(int, int, double, double);
static int do_read_config(const char *);
static int get_fd_len(int);
static int parse_buf(char *, size_t);
static int eval_cmd(const char *, const char *);
static int eval_val(struct token_map, const char *);
static int read_config(const char *);
static int read_opts(int, char **);
static double str_to_double(const char *);


const struct token_map tok_map[] = {
  { "coin",     TOK_COIN,     TYPE_STR },
  { "fiat",     TOK_FIAT,     TYPE_STR },
  { "holdings", TOK_HOLDINGS, TYPE_DOUBLE } 
};

const struct fiat_map f_map[] = {
  { "gbp", "\u00a3", GBP },
  { "usd", "\u0024", USD },
  { "eur", "\u20ac", EUR },
};

const struct coin_map c_map[] = {
  /* the url's are incomplete and need a fiat currency added to the end i.e "vs_currencies=usd" */
  { "btc", "bitcoin",  BTC, "https://api.coingecko.com/api/v3/simple/price?ids=bitcoin&vs_currencies=" },
  { "xmr", "monero",   XMR, "https://api.coingecko.com/api/v3/simple/price?ids=monero&vs_currencies=" },
  { "ltc", "litecoin", LTC, "https://api.coingecko.com/api/v3/simple/price?ids=litecoin&vs_currencies=" },
  { "eth", "ethereum", ETH, "https://api.coingecko.com/api/v3/simple/price?ids=ethereum&vs_currencies=" },
  { "doge", "doge", DOGE, "https://api.coingecko.com/api/v3/simple/price?ids=doge&vs_currencies=" },
  { "bch", "bitcoin-cash",  BCH, "https://api.coingecko.com/api/v3/simple/price?ids=bitcoin-cash&vs_currencies=" },
  { "usdt", "tether",  USDT, "https://api.coingecko.com/api/v3/simple/price?ids=tether&vs_currencies=" },
  { "wow",  "wownero", WOW,  "https://api.coingecko.com/api/v3/simple/price?ids=wownero&vs_currencies=" },
};

double conf_holdings  = 0.0f;
int conf_mia          = 0;   /* see if we have a config file or not */
int conf_coin         = BTC; /* the crypto we will be working with */
int conf_fiat         = USD; /* the fiat we will be working with */
int opt_ctf           = 1;   /* crypto->fiat or fiat->crypto */
int opt_holdings      = 0;   /* to display holdings of X currency in Y currency */
int opt_display_price = 1;   /* to display price of a crypto currency in a fiat currency */
int opt_help          = 0;

int
main(int argc, char **argv)
{
  char *json, *url;
  double price = 0.0f;

  read_config((char *) 0);

  if (read_opts(argc, argv) < 0) {
    if (opt_help)
      return help(argv[0]);

    return -1;
  }

  json = url = (char *) 0;

  /* append the fiat currency to the url */
  url = append_str(c_map[conf_coin].url, f_map[conf_fiat].name);  
  
  /* get the price data in json format from coin gecko */
  if (!(json = get_page(url)))
    return -1;

  free(url);

  price = json_get_price(conf_coin, conf_fiat, json);

  free(json);

  /*
   * if its 0 to 8 digits we can assume there was an error in getting the price
   * coingecko likes to timeout if we spam requests TODO
   */
  if (price == 0.00f) {
    fprintf(stderr,
    "network error : coingecko timed out : try again in 60 seconds\n");
    return -1;
  }

  if (opt_display_price == 1) {
    display_price(conf_coin, conf_fiat, price);
    return 0;
  } else if (opt_holdings == 1) {
    display_holdings(conf_coin, conf_fiat, conf_holdings, price);
    return 0; 
  }

  return 0;
}

static int
help(const char *cmd)
{
  printf("%s usage:\n-f <fiat currency>\n-c <crypto currency>\n", cmd);
  printf("-a <amount of X currency> : will use this value in calculations\n");
  printf("-b to display the amount of a crypto currency you hold in fiat currency\n");
  printf("-C to do crypto->fiat conversions\n");
  printf("-F to do fiat->crypto conversions\n");
  printf("-p to display the price of a crypto currency\n");
  printf("-h to display this help message\n\n");
  puts("Available coins:");

  for (int i = 0; i < (int) ARRAY_MAX(c_map); i++)
    puts(c_map[i].symbol);

  printf("\nAvailable fiat currencies:\n");

  for (int i = 0; i < (int) ARRAY_MAX(f_map); i++)
    puts(f_map[i].name);

  return 0;
}

static char *
get_page(const char *url)
{
  CURL *c_handle;
  CURLcode c_result;
  struct page_data p_data;

  if (!url)
    return (char *) 0;

  p_data.buf = (char *) malloc(1);
  p_data.len = 0;

  c_handle = curl_easy_init();

  if (c_handle) {
    curl_easy_setopt(c_handle, CURLOPT_URL, url);
    curl_easy_setopt(c_handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c_handle, CURLOPT_WRITEFUNCTION, write_page_data);
    curl_easy_setopt(c_handle, CURLOPT_WRITEDATA, (void *) &p_data);
    curl_easy_setopt(c_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

    c_result = curl_easy_perform(c_handle);

    if (c_result != CURLE_OK)
      fprintf(stderr, "%s error : %s\n", __func__, curl_easy_strerror(c_result));

    curl_easy_cleanup(c_handle);

    return p_data.buf;
  }

  return (char *) 0;
}

size_t
write_page_data(void *data, size_t len, size_t n_mem, void *user_ptr)
{ 
  char *buf;
  size_t actual_len;
  struct page_data *p_data;

  actual_len = len * n_mem;
  p_data = (struct page_data *) user_ptr;

  if (!(buf = realloc(p_data->buf, p_data->len + actual_len + 1))) {
    fprintf(stderr, "%s : Failed to allocate memory\n", __func__);
    return 0;
  }

  p_data->buf = buf;
  memcpy(&(p_data->buf[p_data->len]), data, actual_len);
  p_data->len += actual_len;
  p_data->buf[p_data->len] = 0;

  return actual_len;
}

static char *
append_str(char *to, char *from)
{
  char *new;
  size_t n_len;

  n_len = strlen(to) + strlen(from) + 1;

  if (!(new = (char *) malloc(n_len))) {
    fprintf(stderr, "%s error: Failed to allocate memory\n", __func__);
    return new;
  }

  new[0] = (char) 0;
  strlcat(new, to, n_len);
  strlcat(new, from, n_len);

  return new;
}

static double
json_get_price(int coin_d, int fiat_d, const char *buf)
{
  /*
   * really rigid function for getting the price of the crypto
   * I say rigid as its going off the assumption that coin gecko
   * will return something like "{"bitcoin":{"usd":43123}}"
   * not dynamic at all and will probably need work in future as things change
   */
  cJSON *root;
  double val = 0.0f;

  if (!(root = cJSON_Parse(buf)))
    return val;

  cJSON *coin = cJSON_GetObjectItem(root, c_map[coin_d].full);
  cJSON *fiat = cJSON_GetObjectItem(coin, f_map[fiat_d].name); 
  
  if (cJSON_IsNumber(fiat)) {
    val = fiat->valuedouble;
  }

  return val;
}

static void
display_price(int coin, int fiat, double price)
{
  fprintf(stdout, "1 %s = %s%0.11f\n",
    c_map[coin].symbol, f_map[fiat].symbol, price);
}

static void
display_holdings(int coin, int fiat, double holdings, double price)
{
  double holdings_price;

  if (opt_ctf == 1) {
    holdings_price = price * holdings;
    fprintf(stdout, "%0.11f %s = %s%0.11f\n",
      holdings, c_map[coin].symbol, f_map[fiat].symbol, holdings_price);
  } else {
    holdings_price = holdings / price;
    fprintf(stdout, "%s%0.11f = %0.11f %s\n",
      f_map[fiat].symbol, holdings, holdings_price, c_map[coin].symbol);
  }
}

static int
read_config(const char *path)
{
  int ret = -1;
  char *home_dir, *full_paths[3];

  /* going to keep this rigid for headaches sake */
  char *conf_paths[] = {
    "/.config/coinprice/config",
    "/.config/coinprice.conf",
    "/.coinprice.conf",
  };

  home_dir = getenv("HOME");

  if (!home_dir)
    return ret;

  if (!(full_paths[0] = append_str(home_dir, conf_paths[0])) ||
    !(full_paths[1] = append_str(home_dir, conf_paths[1])) ||
    !(full_paths[2] = append_str(home_dir, conf_paths[2]))) {
    return ret;
  }

  if (!path) {
    for (int i = 0; i < (int) ARRAY_MAX(full_paths); i++) {
      if (access(full_paths[i], F_OK) == 0) /* check path exists */ {
        ret = do_read_config(full_paths[i]);
        conf_mia = 0;
        goto exit;
      }
    }

    conf_mia = 1; /* failed to find a config file */
  } else
    ret = do_read_config(path);
  
exit:
  for (int i = 0; i < (int) ARRAY_MAX(full_paths); i++)
    free(full_paths[i]);

  return ret;
}

static int
do_read_config(const char *path)
{
  int fd, len, index;
  char c, *buf = (char *) 0;

  fd = len = -1;

  if ((fd = open(path, O_RDONLY)) < 0) {
    fprintf(stderr, "%s : %s : %s\n",
      __func__, path, strerror(errno));
    return -1;
  }

  /* get no of bytes in the file */
  if ((len = (int) get_fd_len(fd)) < 0) {
    close(fd);
    return -1;
  }

  if (!(buf = (char *) malloc((size_t) len))) {
    fprintf(stderr, "%s : %s : failed to allocate memory.\n",
      __func__, path);
    close(fd);
    return -1;
  }

  index = 0;
  memset(buf, 0, (size_t) len);

  /*
   * read byte by byte until reach a \n char 
   * i know this is slow but these config files should not be large or complex
   * so it shouldnt matter, but if the file is large for whatever reason, it
   * shouldnt be so its a user error
   */
  for (int i = 0; i < len; i++) {
    if (read(fd, &c, 1) < 0) {
      fprintf(stderr, "%s : %s : %s\n",
       __func__, path, strerror(errno));
      close(fd);
      return -1;
    }

    if (c == '\n') { /* treat as a line and parse it before moving on */
      if (parse_buf(buf, strlen(buf)) < 0) {
        /* FAIL */
        close(fd);
        free(buf);
        return -1;
      }
      index = 0;
      memset(buf, 0, (size_t) len);
    } else { /* treat as text to be appended to the buffer */
      if (index+1 < len) {
        buf[index] = c;
        buf[index+1] = (char) 0;
        index++;
      }
    }
  } 

  free(buf);
  close(fd);

  return 0;
}

static int
get_fd_len(int fd)
{ 
  off_t len, pos;

  len = pos = -1;

  /* get current position in file */
  if ((pos = lseek(fd, 0, SEEK_CUR)) < 0) {
    fprintf(stderr, "%s : file descriptor %d : %s\n",
      __func__, fd, strerror(errno));
    return (off_t) -1;
  }

  /* reset position in file to 0 */
  if (lseek(fd, 0, SEEK_SET) < 0) {
    fprintf(stderr, "%s : file descriptor %d : %s\n",
      __func__, fd, strerror(errno));
    return (off_t) -1;
  }

  /* seek to end of file to get the no of bytes */
  if ((len = lseek(fd, 0, SEEK_END)) < 0) {
    fprintf(stderr, "%s : file descriptor %d : %s\n",
      __func__, fd, strerror(errno));
    return (off_t) -1;
  }

  /* reset position in file to where it was initially */
  if (lseek(fd, pos, SEEK_SET) < 0) {
    fprintf(stderr, "%s : file descriptor %d : %s\n",
      __func__, fd, strerror(errno));
    return (off_t) -1;
  }

  return len;
}

static int
parse_buf(char *buf, size_t len)
{
  /*
   * the '=' char is going to be the delimeter and there should only be 2 values
   * the "command" and the "value" i.e "coin=btc"
   * we want to to store all the text before and after the '=' in two seperate
   * char*'s including all whitespace so "c o i n = b t c" wouldnt be valid 
   */
  char *cmd, *val;
  int cmd_len, val_len, ret;

  cmd = val = (char *) 0;
  cmd_len = val_len = ret = 0;

  /*
   * go through the string until we hit the delim or not
   * we need to keep count of how many characters we pass before the delim
   * to allocate enough memory for cmd and val
   */
  for (int i = 0; i < (int) len; i++) {
    if (buf[i] == '=') {
      cmd_len = i;
      val_len = (int) len-i;
    }
  }

  if (!(cmd = (char *) malloc(cmd_len+1)) ||
    !(val = (char *) malloc(val_len+1))) {
    fprintf(stderr, "%s : %d : failed to allocate memory\n",
      __func__, val_len+cmd_len+2);
    return -1;
  }

  /* get the command */
  for (int i = 0; i < cmd_len; i++)
    cmd[i] = buf[i];

  cmd[cmd_len] = (char) 0;

  /* get the value */
  for (int i = cmd_len+1, k = 0; k < val_len; i++, k++)
    val[k] = buf[i];

  val[val_len] = (char) 0;

  ret = eval_cmd(cmd, val);

  free(cmd);
  free(val);

  return ret;
}

static int
eval_cmd(const char *cmd, const char *val)
{
  for (int i = 0; i < (int) ARRAY_MAX(tok_map); i++)
    if (!strcmp(cmd, tok_map[i].cmd)) {
      if (eval_val(tok_map[i], val) != 0) /* eval_tok will print errors */
        return -1;
      else
        return 0;
    }

  /* FAIL */
  fprintf(stderr, "config error : no such config option \"%s\"\n", cmd);
  return -1;
}

static int
eval_val(struct token_map tm, const char *val)
{
  switch (tm.val_type) {
    case TYPE_STR:
      switch (tm.tok_type) {
        case TOK_COIN:
          for (int i = 0; i < (int) ARRAY_MAX(c_map); i++) {
            if (!strncmp(val, c_map[i].symbol, strlen(c_map[i].symbol))) {
              conf_coin = c_map[i].enum_val;
              goto success;
            }
          }
          /* FAIL */
          fprintf(stderr, "config error : unknown coin \"%s\"\n", val);
          return -1;
        case TOK_FIAT:
          for (int i = 0; i < (int) ARRAY_MAX(f_map); i++) {
            if (!strncmp(val, f_map[i].name, strlen(f_map[i].name))) {
              conf_fiat = f_map[i].enum_val;
              goto success;
            }
          }
          /* FAIL */
          fprintf(stderr, "config error : unknown fiat currency \"%s\"\n", val);
          return -1;
      }
      break;
    case TYPE_DOUBLE:
      switch (tm.tok_type) { /* right now only holdings is a double, future proofing */
        case TOK_HOLDINGS:
          conf_holdings = str_to_double(val);
          break;
      }
      break;
  }
success:
  return 0;
}

static int
read_opts(int argc, char **argv)
{
  for (int i = 1; i < argc; i++) {
    if (argv[i][0] == '-' && argv[i][1] != (char) 0) { /* found an option */
      switch (argv[i][1]) {
        case 'a': /* amount aka holdings of either fiat or crypto, 1 arg */
          if (!argv[i+1]) { /* no next arg, fail */
            fprintf(stderr, "option error : \"-%c\" requires 1 argument\n",
              argv[i][1]);
            return -1;
          }

          /* attempt to set the value using the config file functions  */
          if (eval_val((struct token_map) { "holdings", TOK_HOLDINGS, TYPE_DOUBLE },
            argv[i+1]) < 0)
            return -1;

          opt_display_price = 0;
          opt_holdings = 1;

          i++;
          break;
        case 'b': /* display holdings (b for balance) */
          opt_display_price = 0;
          opt_holdings = 1;
          break;
        case 'c': /* crypto currency (coin), requires 1 arg */
          if (!argv[i+1]) { /* no next arg, fail */
            fprintf(stderr, "option error : \"-%c\" requires 1 argument\n",
              argv[i][1]);
            return -1;
          }

          /* attempt to set the value using the config file functions  */
          if (eval_val((struct token_map) { "coin", TOK_COIN, TYPE_STR },
            argv[i+1]) < 0)
            return -1;

          i++;
          break;
        case 'C': /* holdings is in crypto (default), 0 args */
          opt_ctf = 1;
          break;
        case 'f': /* set fiat currency, requires 1 arg */
          if (!argv[i+1]) { /* no next arg, fail */
            fprintf(stderr, "option error : \"-%c\" requires 1 argument\n",
              argv[i][1]);
            return -1;
          }

          /* attempt to set the value using the config file functions  */
          if (eval_val((struct token_map) { "fiat", TOK_FIAT, TYPE_STR },
            argv[i+1]) < 0)
            return -1;

          i++;
          break;
        case 'F': /* holdings is in fiat, 0 args */
          opt_ctf = 0;
          break;
        case 'h':
          opt_help = 1;
          return -1;
        case 'p': /* display price of whatever, 0 args */
          opt_display_price = 1;
          opt_holdings = 0;
          break;
        default: /* FAIL */
          fprintf(stderr, "option error : no such option \"%s\"\n", argv[i]);
          return -1;
      }
    }
  }

  return 0;
}

static double
str_to_double(const char *buf)
{
  /* TODO maybe do some checks on buf to see if its a valid double */
  return strtod(buf, (char **) 0);
}
