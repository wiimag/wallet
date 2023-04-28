# Changelog

## [0.19.8] - 2023-04-27
- Add basic fundamental pattern analysis.
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

- [ ] 2023-04-27: Check alerts in a job
- [ ] 2023-04-20: Use the new search window to add a new title
- [ ] 2023-04-20: Check if we can download a first search index database from the backend
- [ ] 2023-04-19: Add user guide
- [ ] 2023-04-19: Add user authentication when using the proxy backend
- [ ] 2023-04-19: Add augmented logo cache from the backend
