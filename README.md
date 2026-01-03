This is a small hacky tool I made to work with crypto currency prices in a UNIX shell.

You are able to check prices of whatever coins, convert the price of X amount of crypto into fiat, or convert the price of X amount of fiat into a crypto.

It uses libcurl to get price data from coingecko and then a basic json parser to get the price.

I wrote it in about 4 hours so its pretty messy.

It requires libcurl and cJSON.

Basic usage would be something like "crypto-conv xmr gbp 20 ftc"

This would display £20 worth of Monero.

Another example would be "crypto-conv ltc usd 4.2 ctf"

This would display 4.2 Litecoin in USD.
