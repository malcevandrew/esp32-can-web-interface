let ws;

function initWebSocket() {
    // Автоматически вычисляем адрес WebSocket сервера на основе IP платы
    ws = new WebSocket(`ws://${window.location.host}/ws`);

    ws.onopen = () => {
        const stateEl = document.getElementById("ws-state");
        stateEl.textContent = "CONNECTED";
        stateEl.className = "status-online";
    };

    ws.onclose = () => {
        const stateEl = document.getElementById("ws-state");
        stateEl.textContent = "DISCONNECTED";
        stateEl.className = "status-offline";
        setTimeout(initWebSocket, 2000); // Авто-реконнект при обрыве связи
    };

    ws.onmessage = (event) => {
        let data;
        try {
            data = JSON.parse(event.data);
        } catch (error) {
            console.error("Invalid telemetry JSON:", error);
            return;
        }

        // Извлекаем объект шаговика напрямую по ключу, а не через forEach
        const stepper = data.stepper;
        if (stepper) {
            document.getElementById("step-state").textContent = stepper.state ?? "UNKNOWN";
            document.getElementById("step-pos").textContent = stepper.position ?? 0;

            // Корректно обрабатываем отсутствие целевой позиции в текущей версии прошивки
            if (document.getElementById("step-tgt")) {
                document.getElementById("step-tgt").textContent = "—";
            }

            // Логика динамической подсветки аварии (FAULT)
            const errElement = document.getElementById("step-err");
            if (errElement) {
                if (stepper.state === "FAULT") {
                    errElement.textContent = "CRITICAL FAULT";
                    errElement.className = "status-offline"; // Красный стиль из CSS
                } else {
                    errElement.textContent = "—";
                    errElement.className = "status-ok";      // Зеленый стиль из CSS
                }
            }
        }

        // Извлекаем объект актуатора (заглушка)
        const actuator = data.actuator;
        if (actuator) {
            document.getElementById("act-state").textContent = actuator.state ?? "UNKNOWN";
            document.getElementById("act-pos").textContent = actuator.position ?? 0;
            if (document.getElementById("act-tgt")) {
                document.getElementById("act-tgt").textContent = "—";
            }
        }
    };

}

function sendCmd(deviceName, commandName) {
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify({ device: deviceName, command: commandName }));
    }
}

function sendMove(deviceName, commandName) {
    if (ws && ws.readyState === WebSocket.OPEN) {
        const val = parseInt(document.getElementById("step-input").value);
        ws.send(JSON.stringify({ device: deviceName, command: commandName, position: val }));
    }
}

function sendActuatorPreset(percent) {
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify({ device: "actuator", command: "goto", position: percent }));
    }
}

document.addEventListener("DOMContentLoaded", initWebSocket);
