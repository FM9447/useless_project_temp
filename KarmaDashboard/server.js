const express = require('express');
const http = require('http');
const WebSocket = require('ws');
const { SerialPort, ReadlineParser } = require('serialport');

const app = express();
const server = http.createServer(app);
const wss = new WebSocket.Server({ server });

app.use(express.static('public'));

// Update 'COM3' to match your ESP32's actual serial port
const portName = 'COM3';
const baudRate = 115200;

const serialPort = new SerialPort({ path: portName, baudRate: baudRate });
const parser = serialPort.pipe(new ReadlineParser({ delimiter: '\r\n' }));

parser.on('data', (data) => {
    wss.clients.forEach((client) => {
        if (client.readyState === WebSocket.OPEN) {
            client.send(data);
        }
    });
});

wss.on('connection', (ws) => {
    ws.on('message', (message) => {
        serialPort.write(message + '\n');
    });
});

server.listen(3000, () => {
    console.log('Karma Dashboard running at http://localhost:3000');
});