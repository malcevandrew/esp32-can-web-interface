let ws;

 // Вызывается один раз при загрузке страницы
function initWebSocket() {
    ws = new WebSocket(`ws://${window.location.host}/ws`);

    ws.onopen = () => {
        const stateEl = document.getElementById("ws-state");
        stateEl.textContent = "CONNECTED";          // Обновляем поле с текущим состоянием WebSocket
        stateEl.className = "status-online";        // Обычный цвет шрифта
    };

    ws.onclose = () => {
        const stateEl = document.getElementById("ws-state");
        stateEl.textContent = "DISCONNECTED";       // Обновляем поле с текущим состоянием WebSocket
        stateEl.className = "status-offline";       // Подсвечиваем шрифт красным 
        setTimeout(initWebSocket, 2000);
    };

    // Асинхронная функция-обработчик JSON-пакета с новыми координатами.
    // Выполняется всякий раз при полученнии телеметрии с ЕСП32
    ws.onmessage = (event) => {     
        
        // Парсим JSON-пакет
        let data;
        try {
            data = JSON.parse(event.data);
        } catch (error) {
            console.error("Invalid telemetry JSON:", error);
            return;
        }

        // Извлекаем и обновляем текущее состояние шаговика
        const stepper = data.stepper;
        if (stepper) {
            document.getElementById("step-state").textContent = stepper.state ?? "UNKNOWN";     // Обновляем поле с текущим состоянием
            document.getElementById("step-pos").textContent = stepper.position ?? 0;            // Обновляем поле с текущей координатой

            if (stepper.target !== undefined) {
                 document.getElementById("step-tgt").textContent = stepper.target;              // Обновляем поле с текущей целью
            }

            // Подсветка критических аварий (FAULT или LINK_ERROR)
            const errElement = document.getElementById("step-err");
            if (errElement) {
                if (stepper.state === "FAULT" || stepper.state === "LINK_ERROR") {   // Если критическое состояние     
                    errElement.textContent = stepper.state;                          // Обновляем поле с текущим состояниеме
                    errElement.className = "status-offline";                         // Подсвечиваем шрифт красным                      
                } else {
                    errElement.textContent = "—";
                    errElement.className = "status-ok";      
                }
            }
        }
/*
        // Извлекаем объект актуатора (заготовка для следущей версии)
        const actuator = data.actuator;
        if (actuator) {
            document.getElementById("act-state").textContent = actuator.state ?? "UNKNOWN";
            document.getElementById("act-pos").textContent = actuator.position ?? 0;
            if (document.getElementById("act-tgt")) {
                document.getElementById("act-tgt").textContent = "—";
            }
        }
*/
        // Обновление блока с информацией о текущем шаге (шаг 1 из 4) в режиме "Последовательность"
        const progBlock = document.getElementById("prog-status-block");
        if (progBlock) {
             if (data.program_running) {
                progBlock.style.display = "block";  // Отображаем блок, если последовательность запущена
                document.getElementById("prog-current-step").textContent = data.program_step ?? 0;  // Обновляем поле "Текущий шаг"
                document.getElementById("prog-total-steps").textContent = data.program_total ?? 0;  // Обновляем поле "Всего шагов"
                
                // Блокировка ручного интерфейса во время выполнения цикла
                toggleManualControls(true);
            } else {
                progBlock.style.display = "none";
                toggleManualControls(false);
            }
        }
    };
}

// Блокировка ручного интерфейса во время выполнения цикла
function toggleManualControls(disable) {
    document.getElementById("btn-start-prog").disabled = disable;
    document.getElementById("btn-goto").disabled = disable;
    document.getElementById("btn-home").disabled = disable;
    document.getElementById("step-input").disabled = disable;
    document.getElementById("prog-input").disabled = disable;
}

// Обработчик нажития на кнопку "HOME" или "STOP"
function sendCmd(deviceName, commandName) {
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify({ device: deviceName, command: commandName }));                  // Формируем JSON-пакет и отправляем
    }
}

// Обработчик нажития на кнопку "GOTO"
function sendMove(deviceName, commandName) {
    if (ws && ws.readyState === WebSocket.OPEN) {
        const val = parseInt(document.getElementById("step-input").value);                      // Берем введеное в форму "GOTO" значение
        ws.send(JSON.stringify({ device: deviceName, command: commandName, position: val }));   // Формируем JSON-пакет и отправляем
    }
}

// Обработчик нажития на кнопку "START SEQUENCE"
// Парсит строку (например, "20, 60, 15") в массив чисел и шлет на ESP32
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

// Обработчик нажития на кнопку "STOP SEQUENCE"
function stopAutomatedProgram() {
    if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify({ command: "stop_program" }));
    }
}

// Инициализация WebSocket при загрузке страницы
document.addEventListener("DOMContentLoaded", initWebSocket);   