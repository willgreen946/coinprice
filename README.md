This is a small hacky tool I made to work with crypto currency prices in a UNIX shell.

You are able to check prices of whatever coins, convert the price of X amount of crypto into fiat, or convert the price of X amount of fiat into a crypto.

It uses libcurl to get price data from coingecko and then a basic json parser to get the price.

I wrote it in about 4 hours so its pretty messy.

It requires libcurl and cJSON.

Basic usage would be something like "coinprice -f gbp -c xmr -a 55 -F"

This would display £55 worth of Monero.

Another example would be "coinprice -c ltc -c usd -a 4.2"

This would display 4.2 Litecoin in USD.

There is a basic config file system where a user can have a config file in one of these locations:
~/.config/coinprice/coinprice.conf
~/.config/coinprice.conf
~/.coinprice.conf

The syntax is very simple, and example config would look like this:
coin=ltc
holdings=22.9221
fiat=eur

This will mean the program will run with the default coin as Litecoin, the holdings amount as 22.9221 and the fiat currency as Euros.
The program will now use these values before evaluating any command line arguments, this makes it useful for scripts or just quick usage.
