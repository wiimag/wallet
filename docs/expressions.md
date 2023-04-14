# Expressions

This document illustrates the use of expressions in the Wallet application.

## Introduction

First a few words about expressions. An expression is a string that is evaluated to a value. The value can be a number, a string, a boolean, or a list of values. The expression can be used in the following places:

- In the console window,
- in the `--eval` command,
- and in the report column expressions.

## Functions

### S($symbol, $field)

The `S(...)` function is used to get a value from a stock symbol. The first argument is the symbol name, and the second argument is the field name. The field name can be one of the following:

- `open` - the opening price of the stock,
- `high` - the highest price of the stock,
- `low` - the lowest price of the stock,
- `high` - the highest price of the stock,
- `price` - the current price of the stock,
- `close` - the closing price of the stock,
- `date` - the date of the current price from the real-time data,
- `gmt` - the GMT offset of the current price from the real-time data,
- `volume` - the volume of the current price from the real-time data,
- `yesterday` - the closing price of the previous day,
- `change` - the change in $ from `open` to `close`,
- `change_p` - the change in % from `open` to `close` (value is already multiplied by 100),
- `change_p_high` - the change in % from `high` to `close` (value is already multiplied by 100),
- `wma` - the weighted moving average,
- `ema` - the exponential moving average,
- `sma` - the simple moving average,
- `uband` - the upper band of the Bollinger Bands,
- `mband` - the middle band of the Bollinger Bands,
- `lband` - the lower band of the Bollinger Bands,
- `sar` - the parabolic SAR,
- `slope` - the slope of the linear regression,
- `cci` - the commodity channel index,
- `dividends` - the dividends yield ìn % (the value is **not** multiplied by 100),
- `earning_trend_actual` - the actual earning trend,
- `earning_trend_estimate` - the estimated earning trend,
- `earning_trend_difference` - the difference between the actual and the estimated earning trend,
- `earning_trend_percent` - the percent difference between the actual and the estimated earning trend,
- `name` - the name of the stock,
- `description` - the description of the stock,
- `country` - the country of the stock,
- `sector` - the sector of the stock,
- `industry` - the industry of the stock,
- `type` - the type of the stock,
- `currency` - the currency of the stock,
- `url` - the URL of the stock,
- `updated_at` - the date for which we have the most recent data for the stock,
- `exchange` - the exchange of the stock,
- `symbol` - the ticker/symbol of the stock,
- `shares_count` - the number of shares of the stock,
- `low_52` - the lowest price of the stock in the last 52 weeks,
- `high_52` - the highest price of the stock in the last 52 weeks,
- `pe` - the price/earnings ratio,
- `peg` - the price/earnings to growth ratio,
- `ws_target` - the Wall Street target price,
- `beta` - the beta of the stock,
- `dma_50` - the 50-day moving average,
- `dma_200` - the 200-day moving average,
- `revenue_per_share_ttm` - the revenue per share for the trailing 12 months,
- `profit_margin` - the profit margin,
- `trailing_pe` - the trailing price/earnings ratio,
- `forward_pe` - the forward price/earnings ratio,
- `short_ratio` - the short ratio,
- `short_percent` - the short percent,
- `diluted_eps_ttm` - the diluted earnings per share for the trailing 12 months

#### Examples

```
S("AAPL.US", "price") => 164.15
S("U.US", "change") => -0.415
S("U.US", "change_p") => -1.4082
S("U.US", "change_p_high") => 4.17374
S("U.US", "wma") => 30.4493
S("GFL.TO", "ema") => 44.0917
```	

#### Column Expressions

**Split:** `S($TITLE, price_factor)`
**Ready:** `ROUND((S($TITLE, close) / ((S($TITLE, dma_200)+S($TITLE, dma_50))/2)) * 100)`
**DMA:** `MAX(S($TITLE, dma_50), S($TITLE, dma_200))`

### S($symbol, $field, $date)

This variante of the `S(...)` function is used to get a value from a stock symbol for a specific date. The first argument is the symbol name, the second argument is the field name, and the third argument is the date. The date can be a string in the format `YYYY-MM-DD` or a number of days in the past. The field name can be one of the following:

#### Examples

```
# you can use unix UTC timestamps
S(TSL.TO, wma, 1661497600) => 3.5795

# Or dates in the format "YYYY-MM-DD"
S(PFE.US, close, "2018-01-01") => 28.3434

# The timestamp can always be converted to a date
S(TSL.TO, close, DATESTR(1671497600)) => 3.2456
```	

### R($report, $title, $field)

The `R(...)` function is used to get a value from a report. The first argument is the report name, the second argument is the title symbol ticker, and the third argument is the field name. The field name are the same as for the `S(...)` function above plus the following:

- `sold` - returns `true` if the stock was sold, `false` otherwise,
- `active` - returns `true` if the stock is still active, `false` otherwise,
- `qty` - returns the quantity owned for that title,
- `buy` - returns the average buy cost for that title,
- `day` - returns the day change for that title based on the `qty` owned,
- `buy_total_adjusted_qty` - returns the total adjusted quantity for that title,
- `buy_total_adjusted_price` - returns the total adjusted price for that title,
- `sell_total_adjusted_qty` - returns the total adjusted quantity for that title,
- `sell_total_adjusted_price` - returns the total adjusted price for that title,
- `buy_total_price` - returns the total acquisition price for that title,
- `buy_total_quantity` - returns the total acquisition quantity for that title,
- `sell_total_price` - returns the total sale price for that title,
- `sell_total_quantity` - returns the total sale quantity for that title,
- `buy_total_price_rated_adjusted` - returns the total acquisition price for that title (the price is multiplied by the quantity and exchange rate if needed),
- `sell_total_price_rated_adjusted` - returns the total sale price for that title (the price is multiplied by the quantity and exchange rate if needed),
- `buy_total_price_rated` - returns the total acquisition price for that title (the price is multiplied by the quantity),
- `sell_total_price_rated` - returns the total sale price for that title (the price is multiplied by the quantity),
- `buy_adjusted_price` - returns the adjusted price if the titles are bought in diffrent currencies and dates (takes into accounbt splits, dividends, etc.), otherwise returns the same as `buy_total_price_rated_adjusted`,
- `sell_adjusted_price` - returns the adjusted price if the titles are sold in diffrent currencies and dates (takes into accounbt splits, dividends, etc.), otherwise returns the same as `sell_total_price_rated_adjusted`,
- `average_price` - returns the average price for that title,
- `average_price_rated` - returns the average price for that title (the price is multiplied by the quantity and exchange rate if needed),
- `average_quantity` - returns the average quantity for that title,
- `average_buy_price` - returns the average buy price for that title,
- `average_buy_price_rated` - returns the average buy price for that title (the price is multiplied by the quantity and exchange rate if needed),
- `remaining_shares` - returns the remaining shares for that title in case some were sold,
- `total_dividends` - returns the total dividends for that title,
- `average_ask_price` - returns the average ask price for that title,
- `average_ask_price_rated` - returns the average ask price for that title (the price is multiplied by the quantity and exchange rate if needed),
- `date_min` - returns the date of the first transaction for that title,
- `date_max` - returns the date of the most recent transaction for that title,
- `date_average` - returns the average date for that title (quantity are not weighted),
- TODO: `date_average_weighted` - returns the average date for that title (quantity are weighted),
- `title` - returns the title symbol/ticker code,
- `ps` - returns the prediction strength for that title,
- `ask` - returns the ask price for that title,
- `today_exchange_rate` - returns the exchange rate for that title for the current date,

#### Examples

```
R("300K", "TSLA.US", "close") => 3.2456
R('300K', 'TNT-UN.TO', dividends) => 0.0887
R('300K', 'GFL.TO', sold) => true
```	

#### Column Expressions

**Recent:** `R($REPORT, $TITLE, date_max)`
**Ask II:** `R($REPORT, $TITLE, ask) * (1 + (1 - ((S($TITLE, close) / ((S($TITLE, dma_200)+S($TITLE, dma_50))/2)))))`
**Dividends:** `R($REPORT, $TITLE, buy_total_price) - R($REPORT, $TITLE, buy_total_adjusted_price)`

### F($symbol, $field)

The `F(...)` function is used to get a fundamental value from a symbol. The first argument is the symbol name, and the second argument is the field name. The field name can be about anything return by the /api/fundamentals endpoint.

#### Examples

```
# Get the stock beta
F(TSLA.US, "Technicals.Beta") #=> 2.0705
```

```
# Get the stock industry
F(TSLA.US, "General.Industry") #=> Auto Manufacturers
```

#### Column Expressions

**Beta:** `ROUND(F($TITLE, "Technicals.Beta") * 100)`
