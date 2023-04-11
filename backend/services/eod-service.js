/*
 * Copyright 2023 Wiimag Inc. All rights reserved.
 * License: https://equals-forty-two.com/LICENSE
 * 
 * Service used to proxy HTTP requests to the EOD API.
 * 
 * Based on the URL we cache the response for a given amount of time.
 * 
 * We cache the responses under cache/<url-end-point-hash>.json
 */
// @ts-check

const _ = require('lodash'),
    fs = require('fs/promises'),
    path = require('path');

const { readdir, stat } = require('fs/promises');
const constants = require('fs').constants;
const HttpService = require('./http-service');
const ApplicationService = require('./application-service');

const minimist = require('minimist');
const opts = minimist(process.argv.slice(2));

const proxy = require('http-proxy-middleware');

const SECONDS = 1000;
const MINUTES = 60 * SECONDS;
const HOURS = 60 * MINUTES;
const DAYS = 24 * HOURS;
const WORKING_DAYS = 8 * HOURS;
const WEEKS = 5 * DAYS;
const MONTHS = 25 * DAYS;

/**
 * @typedef {string} DbFilePath - Artifact database file path
 */

const dirSize = async dir => {
    const files = await readdir( dir, { withFileTypes: true } );

    const paths = files.map( async file => {
        const dir_path = path.join( dir, file.name );

        if ( file.isDirectory() ) 
            return await dirSize( dir_path );

        if ( file.isFile() ) {
            const { size } = await stat( dir_path );
            return size;
        }

        return 0;
    });

    return ( await Promise.all( paths ) ).flat( Infinity ).reduce( ( i, size ) => i + size, 0 );
}

const dirCount = async dirPath => {
    // Count the number of files in a directory recursively
    const files = await readdir(dirPath, { withFileTypes: true });
    
    // @ts-ignore
    return files.reduce(async (count, file) => {
        const filePath = path.join(dirPath, file.name);
        const fileStats = await stat(filePath);
        
        if (fileStats.isDirectory()) {
            return count + await dirCount(filePath);
        } else {
            return count + 1;
        }
    }, 0);
}

class EodService {

    /**
     * @param {ApplicationService} applicationService 
     * @param {HttpService}        httpService 
     */
    constructor(applicationService, httpService) {

        this.httpService = httpService;
        this.applicationService = applicationService;

        this.eodUrl = 'https://eodhistoricaldata.com/';
        this.eodApiKey = process.env["EOD_API_KEY"] || opts["eod-api-key"];

        this.googleApiUrl = 'https://www.googleapis.com/';
        this.googleApiKey = process.env["GOOGLE_API_KEY"] || opts["google-api-key"];

        this.openAIUrl = 'https://api.openai.com/';
        this.openAIOrg = process.env["OPENAI_ORG_ID"] || opts["openai-org"];
        this.openAIKey = process.env["OPENAI_API_KEY"] || opts["openai-api-key"];

        // Check that the EOD API key is set
        if (!this.eodApiKey) {
            throw new Error('EOD_API_KEY environment variable is not set.');
        }

        // Create the cache artifact directory
        this.cacheDir = path.join(this.applicationService.appDir(), 'artifacts', 'cache');
        fs.mkdir(this.cacheDir, { recursive: true });

        this.eodProxy = proxy.createProxyMiddleware({
            target: this.eodUrl,
            changeOrigin: true,
            secure: true,
            onProxyReq: (proxyReq, req, res) => this.eodProxyRequest(proxyReq, req, res),
            onProxyRes: (proxyRes, req, res) => this.writeCache(proxyRes, req, res)
        });

        this.googleSearchProxy = proxy.createProxyMiddleware({
            target: this.googleApiUrl,
            changeOrigin: true,
            secure: true,
            onProxyReq: (proxyReq, req, res) => this.googleSearchProxyRequest(proxyReq, req, res),
            onProxyRes: (proxyRes, req, res) => this.writeCache(proxyRes, req, res)
        });

        this.openAIProxy = proxy.createProxyMiddleware({
            changeOrigin: true,
            timeout: 30 * SECONDS,
            followRedirects: true,
            secure: true,
            xfwd: true,
            target: this.openAIUrl,
            onProxyReq: (proxyReq, req, res) => this.openAIProxyRequest(proxyReq, req, res),
            onProxyRes: (proxyRes, req, res) => this.writeCache(proxyRes, req, res),
            onError: (err, req, res) => {
                console.log('Error: ', err);
                return this.httpService.sendError(req, res, 500, `Failed to execute the OpenAI query ${req.path}`, {
                    msg: err.message
                });
            }
        });

        // curl -X GET -s http://localhost/status | json_pp
        httpService.register('get', '/status', this.routeStatus.bind(this));

        // curl -X GET -s http://localhost/api/user | json_pp
        httpService.register('get', '/api/user', this.routeApiUser.bind(this));

        // curl -X GET -s http://localhost/api/eod/U.US | json_pp
        httpService.register('get', '/api/eod/:symbol', this.routeForwardEodRequest.bind(this, 'eod', 1 * WORKING_DAYS));

        // curl -X GET -s http://localhost/api/real-time/U.US | json_pp
        httpService.register('get', '/api/real-time/:symbol', this.routeForwardEodRequest.bind(this, 'realtime', 5 * MINUTES));

        // curl -X GET -s http://localhost/api/technical/U.US?function=splitadjusted | json_pp
        httpService.register('get', '/api/technical/:symbol', this.routeForwardEodRequest.bind(this, 'technical', 1 * WORKING_DAYS));

        // curl -X GET -s http://localhost/api/fundamentals/MSFT.US | json_pp
        httpService.register('get', '/api/fundamentals/:symbol', this.routeForwardEodRequest.bind(this, 'fundamentals', 2 * WEEKS));

        // curl -X GET -s http://localhost/api/eod-bulk-last-day/TO | json_pp
        httpService.register('get', '/api/eod-bulk-last-day/:market', this.routeForwardEodRequest.bind(this, 'bulk', 12 * HOURS));

        // curl -X GET -s http://localhost/api/exchange-symbol-list/TO | json_pp
        httpService.register('get', '/api/exchange-symbol-list/:market', this.routeForwardEodRequest.bind(this, 'symbols', 2 * WEEKS));

        // curl -X GET -s http://localhost/api/exchanges-list | json_pp
        httpService.register('get', '/api/exchanges-list', this.routeForwardEodRequest.bind(this, 'markets', 1 * MONTHS));

        // curl -X GET -s http://localhost/api/calendar/earnings | json_pp
        httpService.register('get', '/api/calendar/earnings', this.routeForwardEodRequest.bind(this, 'calendar', 1 * WEEKS));

        // curl -X GET -s http://localhost/api/search/MSFT?limit=10 | json_pp
        httpService.register('get', '/api/search/*', this.routeForwardEodRequest.bind(this, 'search', 1 * DAYS));

        // curl -X GET -s "http://localhost/api/news?symbol=AAPL.US&limit=3" | json_pp
        httpService.register('get', '/api/news', this.routeForwardEodRequest.bind(this, 'news', 0.25 * DAYS));

        // curl -X GET -s "http://localhost/api/news?symbol=AAPL.US&limit=3" | json_pp
        httpService.register('get', '/customsearch/v1', this.routeForwardGoogleRequest.bind(this, 'gsearch', 0.25 * DAYS));

        // curl -X GET -s "http://localhost/v1/models" | json_pp
        httpService.register('get', '/v1/models', this.routeForwardOpenAIRequest.bind(this, 'models', 2 * WEEKS));

        // curl -X POST http://localhost/v1/completions -d '{ "model": "text-davinci-003",  "prompt": "Testing...\n\nTl;dr", "temperature": 0.7, "max_tokens": 60, "top_p": 1, "frequency_penalty": 0, "presence_penalty": 1 }' -H "Content-Type: application/json"
        httpService.register('post', '/v1/completions', this.routeForwardOpenAIRequest.bind(this, 'completions', 0));

        // TODO: Downlaod search index
        // TODO: Cache thumnails icon and banners
    }

    /**
     * Release db resources when hot reloaded.
     */
    release () {
    }

    /**
     * Build a hash from the URL.
     * @param {string} url - The URL to hash.
     * @returns {number} The hash.
     * @see https://stackoverflow.com/questions/7616461/generate-a-hash-from-string-in-javascript
     */
    urlHash (url) {
        let hash = 0;
        if (url.length == 0) 
            return hash;
        for (let i = 0; i < url.length; i++) {
            let char = url.charCodeAt(i);
            hash = ((hash << 5) - hash) + char;
            hash = hash & hash; // Convert to 32bit integer
        }
        return hash;
    }

    /**
     * Build a file name from the URL and its hash.
     * @param {string} url - The URL to hash.
     * @returns {string} The artifact file name.
     */
    urlFileNameHash (service, url) {
        let hash = this.urlHash(url);
        let endPoint = path.basename(url.split('?')[0]);
        let fileName = endPoint.replace(/\//g, '_');

        return service + '_' + fileName.toLocaleLowerCase() + '_' + hash.toString(16) + '.json';
    }

    /**
     * Check if we have a copy of the response in the cache. Otherwise, forward the request to the EOD API.
     * @param {function} proxy - The proxy function.
     * @param {string} service - The EOD service to call.
     * @param {number} expired - The cache expiration time in milliseconds.
     * @param {HttpService.RequestCacheSettings} req - The HTTP request.
     * @param {HttpService.Response} res - The HTTP response.
     * @param {HttpService.Next} next - The next middleware.
     * @returns {Promise} A promise that resolves when the request is handled.
     */
    readCache (proxy, service, expired, req, res, next) {

        if (expired == 0)
            return proxy(req, res, next);

        let fileName = this.urlFileNameHash(service, req.url);
        let filePath = path.join(this.cacheDir, fileName);

        req.cachePath = filePath;
        req.cacheService = service;

        return fs.stat(filePath).then(stats => {
            // Check if the file is expired
            if (stats.ctimeMs + expired < Date.now())
                throw new Error('File is expired');

            // File exists, read it
            return fs.readFile(filePath, 'utf8');
        }).then((data) => {
            res.json(JSON.parse(data));
        }).catch((err) => {

            // Log error
            console.log(`Failed to read cache file ${filePath}: ${err.message}`);

            // File does not exist, forward request to EOD API
            proxy(req, res, next);
        });
    }

    writeCache (proxyRes, /** @type {HttpService.RequestCacheSettings} */ req, res) {

        // Check if the request is a cacheable request
        if (!req.cachePath)
            return;

        // Add to the response headers that the file was retrieved from the cache
        proxyRes.headers['x-cache-name'] = path.basename(req.cachePath);

        // Check if the response is a JSON
        if (proxyRes.headers['content-type'].indexOf('application/json') != -1) {
            // Cache the response, write the file to disk
            let body = [];
            proxyRes.on('data', (chunk) => {
                body += chunk;
            }).on('end', () => {
                fs.writeFile(req.cachePath, body, { encoding: 'utf8' }).then(() => {
                    console.log('Cached response for ' + req.url + ' to ' + req.cachePath);
                });
            });
        }
    }

    openAIProxyRequest (proxyReq, req, res) {

        // Add keep alive header if not present
        if (!req.headers.connection) {
            proxyReq.setHeader('Connection', 'keep-alive');
        }

        // Check if we have the Authorization header
        if (!req.headers.authorization) {
            proxyReq.setHeader('Authorization', `Bearer ${this.openAIKey}`);
        }

        // Check if we have the OpenAI-Organization header
        if (!req.headers['openai-organization']) {
            proxyReq.setHeader('OpenAI-Organization', this.openAIOrg);
        }
    }

    googleSearchProxyRequest (proxyReq, req, res) {
        // Check if the query param fmt is set to json, if not, then add it
        if (!req.query.cx) {
            // Check if path already contains ? or not
            if (proxyReq.path.indexOf('?') === -1) {
                proxyReq.path = proxyReq.path + '?cx=7363b4123b9a84885';
            } else {
                proxyReq.path = proxyReq.path + '&cx=7363b4123b9a84885';
            }
        }

        // Check if api_token param is equal to wallet or null, if yes, then replace it with the EOD API key                
        if (!req.query.key) {
            // Check if path already contains ? or not
            if (proxyReq.path.indexOf('?') === -1) {
                proxyReq.path = proxyReq.path + '?key=' + this.googleApiKey;
            } else {
                proxyReq.path = proxyReq.path + '&key=' + this.googleApiKey;
            }
        }
    }

    eodProxyRequest (proxyReq, req, res) {
        // Check if the query param fmt is set to json, if not, then add it
        if (!req.query.fmt) {
            // Check if path already contains ? or not
            if (proxyReq.path.indexOf('?') === -1) {
                proxyReq.path = proxyReq.path + '?fmt=json';
            } else {
                proxyReq.path = proxyReq.path + '&fmt=json';
            }
        }

        // Check if api_token param is equal to wallet or null, if yes, then replace it with the EOD API key                
        if (!req.query.api_token) {
            // Check if path already contains ? or not
            if (proxyReq.path.indexOf('?') === -1) {
                proxyReq.path = proxyReq.path + '?api_token=' + this.eodApiKey;
            } else {
                proxyReq.path = proxyReq.path + '&api_token=' + this.eodApiKey;
            }
        } else if (req.query.api_token === 'demo') {
            // Leave as-is
        } else if (!req.query.api_token || req.query.api_token === 'wallet') {
            proxyReq.path = proxyReq.path.replace('api_token=wallet', 'api_token=' + this.eodApiKey);
        }
    }

    /**
     * Handles the DB status HTTP route.
     * @param {HttpService.Request} req 
     * @param {HttpService.Response} res 
     */
    async routeStatus (req, res) {

        let size = await dirSize(this.cacheDir);
        let count = await dirCount(this.cacheDir);
            
        let status = {
            cache: {
                size,
                count,
            }
        };

        res.json(status);
    }

    /**
     * Handles the /api/user HTTP route.
     * @param {HttpService.Request} req
     * @param {HttpService.Response} res
     * @param {HttpService.Next} next
     */
    routeApiUser (req, res, next) {

        // Forward request url path to EOD API as-is
        this.eodProxy(req, res, next);  
    }

    /**
     * Forward request to the EOD API.
     * @param {string} service - The service name.
     * @param {number} expired - The cache expiration time in milliseconds.
     * @param {HttpService.Request} req
     * @param {HttpService.Response} res
     * @param {HttpService.Next} next
     * @returns {Promise}
     */
    routeForwardEodRequest (service, expired, req, res, next) {
        return this.readCache(this.eodProxy, service, expired, req, res, next); 
    }

    /**
     * Forward request to the Google Search API.
     * @param {string} service - The service name.
     * @param {number} expired - The cache expiration time in milliseconds.
     * @param {HttpService.Request} req
     * @param {HttpService.Response} res
     * @param {HttpService.Next} next
     * @returns {Promise}
     * @see https://developers.google.com/custom-search/v1/using_rest
     */
    routeForwardGoogleRequest (service, expired, req, res, next) {
        return this.readCache(this.googleSearchProxy, service, expired, req, res, next); 
    }

    /**
     * Forward request to the OpenAI API.
     * @param {string} service - The service name.
     * @param {number} expired - The cache expiration time in milliseconds.
     * @param {HttpService.Request} req
     * @param {HttpService.Response} res
     * @param {HttpService.Next} next
     * @returns {Promise}
     * @see https://platform.openai.com/docs/api-reference/introduction
     */
    routeForwardOpenAIRequest (service, expired, req, res, next) {
        return this.readCache(this.openAIProxy, service, expired, req, res, next); 
    }
}

module.exports = EodService;
