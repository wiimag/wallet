# Changelog

## [0.19.1] - 2023-04-25
- Add support to check for new releases.
- Fix `system_execute_command` on OSX.
- Fix `time_t` parsing as short date on OSX with `string_template`

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

- [ ] 2023-04-21: Add interface to update stock banner and logo icons.
- [ ] 2023-04-20: Use the new search window to add a new title
- [ ] 2023-04-20: Check if we can download a first search index database from the backend
- [ ] 2023-04-19: Add user guide
- [ ] 2023-04-19: Add user authentication when using the proxy backend
- [ ] 2023-04-19: Add augmented logo cache from the backend
- [ ] 2023-04-19: Add backend to query available releases and versions
