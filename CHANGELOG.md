# Changelog

## [0.24.3] - 2023-05-13
- Add `FETCH(...)` expression to fetch arbitrary data from the backend. (i.e. `COUNT(MAP(FETCH("exchange-symbol-list", "TO"), INDEX($1, 1)))`)
- Add bulk extractor tooling to extract bulk data from EOD and save it to a JSON file.
- Add support to build watch table for stock patterns. See `Pattern/Open Watch Context`.
- Improve INDX display in the pattern and report views.

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
- Fix average buy cost when the stock got sold and we bought it again.
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
