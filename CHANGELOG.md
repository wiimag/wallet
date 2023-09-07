# Changelog

## [0.32.2] - 2023-09-07
- [Add support to register new expression functions from scripts]()
- [Add report column expression text editor](https://github.com/wiimag/wallet/pull/36)
- Add polynomial regression to pattern Revenue view.
- Improve stock financials charting.

## [0.31.6] - 2023-08-25
- Reduce the stock fundamentals cache expiring to 1 day in most cases.
- Add support to plot trend lines using `PLOT($title, $xset, $yset, ..., trend, ...)` expression.
- Add $PATTERN variable when evaluating expressions in the pattern view.
- Add stop limit proposed sell price based on the FLEX ratio as a tooltip in the pattern view.
- Disable search indexing by default. You can enable it in the settings for the desired markets.
- Add support for new intelligent search backend <https://wallet.wiimag.com/xsearch?q=best+mobile+game+engine>
- Add backend analytics support to improve application based on user patterns.

## [0.30.5] - 2023-07-19
- Add setting to cleanup the search index database.
- Read stock news on <https://wallet.wiimag.com/news/SYMBOL>
- Fix P/E display ratio in the pattern view (now using 19 : 1 format instead of an erroneous percentage).
- Compute day gain based on the previous day closing price.
- Add support to copy AI analysis to clipboard.
- [Add support to register and execute script expressions](https://github.com/wiimag/wallet/pull/35)

## [0.29.1] - 2023-07-13
- [Add pattern earnings view](https://github.com/wiimag/wallet/pull/34)

## [0.28.8] - 2023-07-12
- Fix average cost when the stock got sold and we bought it again.
- Fix average days held computation when the stock got sold and we bought it again.
- Fix pattern intraday plot auto fitting.
- Fix report cash balance computation when some stock gets sold but not all of it.
- Improve pattern AI analysis.
- Prompt user at startup when there is a new version available.
- Throttle EOD requests in order to avoid hitting the rate limit.
- Update framework <https://github.com/wiimag/framework/pull/4>
- Update license <https://wiimag.com/LICENSE>
- Use https://eodhistoricaldata.com by default for EOD queries

## [0.27.7] - 2023-06-22
- Fix ask price when days held if lower than 30 days.
- Add support to parse fundamentals values with `F(...)` as numbers even if they are JSON strings.
- Do not index stock with no valuation fundamentals.
- Improve watch window sorting and bulk point insertion.
- Add feedback link under the help menu.
- Add stock fundamentals web link to contextual menu.
- Update how the profit target ratio is computed in the report view.
- Remove support to index ETF and Preferred stocks.

## [0.26.6] - 2023-06-07
- [Add `FORMAT` expression to format expression strings using string templates](https://github.com/wiimag/wallet/pull/33)
- Add support to open dialog windows for the current active floating window (instead of always the main one).
- Fix intraday trend tag annotation position.
- Fix pattern graph trend line equation.
- Fix short number formatting for very small values (see trend line equations).
- Fix window owned dialogs when closing it.
- Improve the pattern fundamentals dialog field value formatting.

## [0.25.13] - 2023-05-29
- Add `LPAD(...)` and `RPAD(...)` functions to pad a string with a given character.
- Add `xtime` options to `PLOT(...)` to plot the x-axis on a time scale.
- Add report dividend management window.
- Add support to download logos from the new https://wallet.wiimag/img/logo/SYMBOL endpoint.
- Fix `MAP(...)` to set the default comparison index to the last index of the array.
- Fix day name french translation.
- Fix initial usage of the "demo" key when lauching the application for the first time.
- Fix timeline total gain computation.
- Improve connectivity state reporting.
- Improve pattern view for ETF stock.
- Remove comments in console before evaluating the expression.

## [0.24.13] - 2023-05-22
- Add `FETCH(...)` expression to fetch arbitrary data from the backend. (i.e. `COUNT(MAP(FETCH("exchange-symbol-list", "TO"), INDEX($1, 1)))`)
- Add bulk extractor tooling to extract bulk data from EOD and save it to a JSON file.
- Add support to build watch table for stock patterns. See `Pattern/Open Watch Context`.
- Fix missing description in the search window when language is default english.
- Fix real-time refresh rate from 5 minutes to 1 minute.
- Improve connectivity to the wallet.wiimag.com backend.
- Improve how the total gain is computed in the wallet view.
- Improve INDX display in the pattern and report views.
- Improve rendering of logos in the add new title dialog.
- Improve report dialogs such as the buy, sell, details dialogs, etc...
- Improve report expression column evaluation by running them asynchronously.
- Update alerts `Date Creation` to display how much time has passed since the alert was created instead of the date.
- Update how downloading the latest version works. We now use the backend to get the latest version number and download the MSI package from the backend. You will need to download the next version from the web site https://wallet.wiimag.com

## [0.23.4] - 2023-05-10
- Add expression file example `report_slope_down.expr` to report titles that are going down but for which you have a gain.
- Add history value trend to the Wallet view.
- Add support to evaluation expression files from the console using `@` followed by a valid file path, i.e. `@C:\work\wallet\docs\expressions\report_slope_down.expr`.
- Add support to use Google Material Design icons for report custom column expressions, i.e. `\\xee\\xa3\\xa3 ...`.
- Fix `TABLE()` date column sorting.
- Improve and simplify the report summary panel.
- Improve the report rename dialog UI.

## [0.22.14] - 2023-05-08
- Add link to web site in the help menu.
- Add support for `YEAR(...)` to get the year of a date. This was useful to plot annual earnings using `PLOT(...)`.
- Add support to automatically update wallet tracking history on a daily basis.
- Add support to ignore invalid symbols from the search index.
- Fix day gain summary taking into account index changes.
- Fix EOD data with erronous closing prices from the data source (i.e. Berkshire Hathaway Inc. (`BRK.A`))
- Fix loading reports overflow of the report directory string.
- Fix logging new wallet history entries on the weekend.
- Fix MSI package launching when looking for updates.
- Fix title average rated buy price rated when no transaction was made.
- Improve `TABLE(...)` and `PLOT(...)` expressions.
- Improve how split factor is computed when the stock just split.
- Improve installation and update process using WiX Toolset.
- Improve report transaction details column setup.

## [0.21.6] - 2023-05-03
- Add `SEARCH(...)` expression to run serach queries.
- Add `TABLE(...)` custom type drawers (i.e. `TABLE('Title', $symbols, ['Symbol', $1, symbol])`) 
- Add intraday pattern view.
- Add table filter search box to the symbols window.
- Add TSX money hyperlink to the pattern view when available.
- Fix EOD data with erronous closing prices from the data source.
- Fix window opened outside of monitor work area.
- Hide Realtime window in non-development builds.
- Improve `R(...)` to support multiple field evaluation (i.e. `R(FLEX, [name, price, S($TITLE, close, NOW() - (90 * 24 * 3600))])`)

## [0.20.12] - 2023-05-01
- Add basic fundamental pattern analysis.
- Add default pattern view track point to follow price changes.
- Add last day bulk data to the search index.
- Add missing french translations.
- Add new version check on startup.
- Add support to localize long date format.
- Add support to translate the stock description in the search window using DeepL.
- Fix contextual menu on invalid titles in a report.
- Fix fetching non existing symbol data.
- Fix report symbol banner image scaling.
- Fix selling quantity than owned.
- Hide pattern plot equation by default.
- Open market symbol in a new window.
- Skip indexing when using demo key.

## [0.19.8] - 2023-04-27
- Add support to check for new releases.
- Add support to update symbol icon and banner logo images.
- Fix crash when closing a floating window.
- Fix opening URL links on MacOS.
- Fix table currency value rounding.
- Fix timestamp parsing on MacOS.
- Improve pattern stats by adding more tooltip information.
- Open financial reports in a new window.
- Remove from query cache files that are 31 days old.
- Update how report titles days help is computed. We weight each transaction dates based on the bought price compared to the total title buy price.

## [0.18.4] - 2023-04-24
- Add support to add titles from the search window to a report.
- Add support to export and import reports.
- Fix OSX compilation issues.
- Fix random symbol menu item exchange encoding.
- Improve search window results by combining EOD /api/search endpoint search results.
- Improve the search tab by using the new search window view.

## [0.17.1] - 2023-04-20
- Add many missing french translations.
- Add support to build the application with or without the proxy backend using `-DBUILD_ENABLE_BACKEND=ON`.
- Add Symbols/Random to open the pattern view of a random stock (just for fun!)
- Fix OpenAI analysis generation when using proxy backend.
- Fix OpenAI analysis generation when using proxy backend.
- Improve wallet history edit dialog.

# TODO
- [ ] 2023-05-07: Notify about split factor changes for a given report (use an alert?)
- [ ] 2023-05-07: Visualize report data in pattern view when owned.
- [ ] 2023-05-06: Improve title statistics when the stock just split.
- [ ] 2023-05-04: Add list of invalid symbols to quickly discard them from the search index
- [ ] 2023-04-27: Check alerts in a job
- [ ] 2023-04-20: Use the new search window to add a new title
- [ ] 2023-04-20: Check if we can download a first search index database from the backend
- [ ] 2023-04-19: Add user guide
- [ ] 2023-04-19: Add user authentication when using the proxy backend
- [ ] 2023-04-19: Add augmented logo cache from the backend
