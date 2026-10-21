let ws;

function initWebSocket() {
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
        setTimeout(initWebSocket, 2000);
    };

    ws.onmessage = (event) => {
        let data;
        try {
            data = JSON.parse(event.data);
        } catch (error) {
            console.error("Invalid telemetry JSON:", error);
            return;
        }

        // 1. Извлекаем объект шаговика
        const stepper = data.stepper;
        if (stepper) {
            document.getElementById("step-state").textContent = stepper.state ?? "UNKNOWN";
            document.getElementById("step-pos").textContent = stepper.position ?? 0;

            // Находим и раскомментируем вывод реальной цели:
            if (stepper.target !== undefined) {
                 document.getElementById("step-tgt").textContent = stepper.target;
            }

            // Логика динамической подсветки критических аварий (FAULT или LINK_ERROR)
            const errElement = document.getElementById("step-err");
            if (errElement) {
                if (stepper.state === "FAULT" || stepper.state === "LINK_ERROR") {
                    errElement.textContent = stepper.state;
                    errElement.className = "status-offline"; 
                } else {
                    errElement.textContent = "—";
                    errElement.className = "status-ok";      
                }
            }
        }

        // 2. Извлекаем объект актуатора
        const actuator = data.actuator;
        if (actuator) {
            document.getElementById("act-state").textContent = actuator.state ?? "UNKNOWN";
            document.getElementById("act-pos").textContent = actuator.position ?? 0;
            if (document.getElementById("act-tgt")) {
                document.getElementById("act-tgt").textContent = "—";
            }
        }

        // 3. ОБРАБОТКА ДАННЫХ АВТОМАТИЗАЦИИ ОТ КООРДИНАТОРА ESP32
        const progBlock = document.getElementById("prog-status-block");
        if (progBlock) {
            if (data.program_running) {
                progBlock.style.display = "block";
                document.getElementById("prog-current-step").textContent = data.program_step ?? 0;
                document.getElementById("prog-total-steps").textContent = data.program_total ?? 0;
                
                // Промышленная блокировка ручного интерфейса во время выполнения цикла
                toggleManualControls(true);
            } else {
                progBlock.style.display = "none";
                toggleManualControls(false);
            }
        }
    };
}

// Вспомогательная функция заморозки кнопок ради безопасности оборудования
function toggleManualControls(disable) {
    document.getElementById("btn-start-prog").disabled = disable;
    document.getElementById("btn-goto").disabled = disable;
    document.getElementById("btn-home").disabled = disable;
    document.getElementById("step-input").disabled = disable;
    document.getElementById("prog-input").disabled = disable;
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

// НОВАЯ ФУНКЦИЯ: Парсит строку (например, "20,60,15") в массив чисел и шлет на ESP32
function startAutomatedProgram() {
    if (ws && ws.readyState === WebSocket.OPEN) {
        const inputStr = document.getElementById("prog-input").value;
        const pointsArray = inputStr.split(',')
                                     .map(p => parseInt(p.trim(), 10))
                                     .filter(p => !isNaN(p));

        if (pointsArray.length === 0) {
            alert("Пожалуйста, введите корректные миллиметры через запятую!");
            return;
        }

        ws.send(JSON.stringify({
            command: "start_program",
            points: pointsArray
        }));
    }
}

// НОВАЯ ФУНКЦИЯ: Принудительный сброс автоматического цикла координатора
function stopAutomatedProgram() {
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify({ command: "stop_program" }));
    }
}

document.addEventListener("DOMContentLoaded", initWebSocket);