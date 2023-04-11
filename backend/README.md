# Install nodejs

```bash
brew install nodejs
```

# Run the backend

> cd backend/
> node main.js --mail-pswd=THEPASSWORD

You should see a log like the following if everything started successfully.

```
Http server started on http://192.168.0.117:80
Created httpService
Created eodService
Created mailService
Created userService
Created ...Service
Created applicationService
Mail server verification successful.
```

Starting the backend for the first time will create a new database. See `artifacts/db.json`

You might want to add a new user for testing into it.

```json
{
    "cases": {},
    "users": {
        "UNIQUE_ID": {
            "apikey": "XYZ",
            "name": "Jonathan Schmidt",
            "email": "joe@infineis.com"
        }
    },
    "files": {}
}
```

Setting an API key is useful for development purposes as it allows you to add it to almost every API (i.e. https://cloud.infineis.com/api/route?apikey=XYZ) in order to execute a query instead of establishing a handshake with the server to get an authentification token. Note that API might return a different dataset than when used with an authentification token for security reasons.

# Test the REST API

Once everything is running you should be able to run a curl request, like so:

```bash
curl -w '\n' http://localhost.infineis.com/api/case/invalid_id?apikey=XYZ
```

# Setup backend on Azure

## Connect to SSH

```powershell
ssh -i ~/.ssh/InfineisBackend_key.pem jonathans@20.169.234.114
```

## Install nodejs and backend

### Install some stuff...
```bash
sudo apt-get install -y build-essential
sudo apt-get update
sudo apt-get upgrade
curl -sL https://deb.nodesource.com/setup_14.x | sudo -E bash -
sudo apt-get install -y npm
sudo apt-get install -y nodejs
sudo npm install -g npm@latest
```

### Make sure you can use port 80 or 443

```bash
sudo apt-get install libcap2-bin 
sudo setcap cap_net_bind_service=+ep `readlink -f \`which node\`` 
```

### Create structure

```bash
cd /var/www/
mkdir infineis
```

#### Copy SSL certificats

Copy SSL certificats to `/var/www/infineis/certs` or generate self sigend certificats

```bash
openssl req -x509 -newkey rsa:4096 -sha256 -days 3650 -nodes \
  -keyout selfsigned.key -out selfsigned.crt -subj "/CN=cloud.infineis.com" \
  -addext "subjectAltName=DNS:cloud.infineis.com,DNS:api.infineis.com,IP:35.203.3.169"
```

### Clone git repo

```bash
git clone git@github.com:Infineis/backend.git
```

> cd backend

> sudo git config --global --add safe.directory /var/www/infineis/backend

> chmod +x main.js

### Setup NPM

```bash
npm install
```

### Make sure to set rights for nobody:nogroup before running the backend

```bash
sudo chown -R nobody:nogroup /var/www/infineis/certs
sudo chown -R nobody:nogroup /var/www/infineis/backend/artifacts
```

### Test running the backend

```bash
sudo node main.js --mail-pswd=XXXXXXX_INSERT_MAIL_PSWD_HERE_XXXXXXX --ip=cloud.infineis.com --use-https
```

## Setup Backend Service

## Create service

> sudo vim /etc/systemd/system/infineis.service

```ini
[Unit]
Description=Infineis
After=network.target

[Service]
ExecStart=/usr/bin/node /var/www/infineis/backend/main.js --ip=cloud.infineis.com --use-https

Restart=always
Type=simple
User=nobody
Group=nogroup
Environment=PATH=/usr/bin:/usr/local/bin
Environment=NODE_ENV=production
Environment=MAIL_PSWD=XXXXXXX_INSERT_MAIL_PSWD_HERE_XXXXXXX
WorkingDirectory=/var/www/infineis/backend

[Install]
WantedBy=multi-user.target
```

### Boot service

```bash
sudo systemctl daemon-reload
sudo systemctl start infineis
sudo systemctl enable infineis
```

### Restart or stop the service

```bash
sudo systemctl restart infineis
```

or

```bash
sudo systemctl stop infineis
```

### Check service status

```bash
systemctl status infineis --no-pager -l
```
