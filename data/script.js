var timeout1, timeout2;

function setSchedule(gate, scheduled) {
    const secondsSinceEpoch = Math.round(Date.now() / 1000);
    const timeDif = scheduled - secondsSinceEpoch;
    if (timeDif > 0) { // only set timer if scheduled time > current time
        console.log(`Schedule for gate ${gate} triggers in ${(timeDif)} s`);
        window[`timeout_${gate}`] = setTimeout(openLeaf.bind(null, gate), timeDif * 1000);
    } else {
        console.log(`Schedule for gate ${gate} expired ${-1*timeDif} s ago`);
    }
}

function openLeaf(g) {
    const leaf = document.getElementById(`leaf_${g}`);
    leaf.classList.add("open");
    notify(`Gate ${g} has been opened!`, 'success');
}


async function toggleIris(e, g) {
    var leaf = e.getElementsByClassName("leaf")[0];
    if (leaf.classList.contains("open")) {
        if (confirm(`Are you sure you want to close gate ${g}?`)) {
            await ajaxGet(`/api/close?g=${g}`);
            leaf.classList.remove("open");
            notify(`Gate ${g} has been closed!`, 'success');
        }
    } else if (confirm(`Are you sure you want to open gate ${g}?`)) {
        await ajaxGet(`/api/open?g=${g}`);
        leaf.classList.add("open");
        notify(`Gate ${g} has been opened!`, 'success');
    }
}

// make ajax request to server
function ajaxGet(url) {
    return new Promise(function(resolve, reject) {
        const xhttp = new XMLHttpRequest();
        xhttp.open("GET", url);
        xhttp.timeout = 2000; // time in milliseconds
        xhttp.onload = function() {
            if (this.status >= 200 && this.status < 300) {
                resolve(xhttp.response);
            } else {
                notify(xhttp.statusText, 'error')
                reject({
                    status: this.status,
                    statusText: xhttp.statusText
                });
            }
        };
        xhttp.onerror = function() {
            notify("An error has occured: " + xhttp.statusText, 'error')
            reject({
                status: this.status,
                statusText: xhttp.statusText
            });
        };
        xhttp.ontimeout = function() {
            notify("A timout has occured: " + xhttp.statusText, 'error')
        }
        xhttp.send();
    });
}

// ---- webUI notifications ----
var notification;

function notify(message, type) {
    const notifyEl = document.getElementById("notify");
    const messageField = notifyEl.getElementsByClassName("message")[0];
    messageField.innerHTML = message;
    notifyEl.classList.value = "show " + type;
    clearTimeout(notification);
    notification = setTimeout(function() { document.getElementById("notify").classList.value = "" }, 4000);
};

window.onload = async function() {
    // Get the current gate states and schedule
    let info = JSON.parse(await ajaxGet("/api/info"));
    console.log(info);

    if (window.location.pathname == '/') {

        const wifiWarning = document.getElementById("wifiWarning");
        if(info.isConnected){
            wifiWarning.style.display = "none";
        }

        // get schedule values and set timers
        const schedule = [
            document.getElementById("schedule_1"),
            document.getElementById("schedule_2")
        ];
        for (let g = 1; g < schedule.length + 1; g++) {
            let t = info[`gate_${g}`].schedule;
            if (t) {
                schedule[g - 1].innerHTML = epoch2str(t);
                setSchedule(g, t);
            } else {
                schedule[g - 1].innerHTML = "not set";
            }
        }

        const leaf1 = document.getElementById("leaf_1");
        const leaf2 = document.getElementById("leaf_2");
        info.gate_1.state ? leaf1.classList.add("open") : leaf1.classList.remove("open");
        info.gate_2.state ? leaf2.classList.add("open") : leaf2.classList.remove("open");
    } else {
        const dt = document.getElementById("serverDateTime");
        if (dt) {
            dt.innerHTML = epoch2str(info.time);
            dt.setAttribute("epoch", info.time);
            setInterval(function() {
                const dt = document.getElementById("serverDateTime");
                let newTime = parseInt(dt.getAttribute("epoch"),10)+1;
                dt.setAttribute('epoch', newTime);
                dt.innerHTML = epoch2str(newTime);
            }, 1000);
        }
    }
}

function str2epoch(ts) {
    // var ts = "2021-11-09T15:00";
    var unix_seconds = ((new Date(ts)).getTime()) / 1000;
    return unix_seconds;
}

function epoch2str(epoch) {
    var d = new Date(0);
    d.setUTCSeconds(epoch);
    return `${d.getDate()}-${d.getMonth()+1}-${d.getFullYear()} ${d.getHours()}:${d.getMinutes()}`
}

async function setDate() {
    const time = str2epoch(document.getElementById("fdate").value);
    const gate = document.querySelector('input[name="fgate"]:checked').value;
    await ajaxGet(`/api/setdate?g=${gate}&t=${time}`);
    notify("Schedule updated", "success");
    document.getElementById("scheduleForm").reset();
    setSchedule(gate, time);
    const schedule = document.getElementById(`schedule_${gate}`);
    schedule.innerHTML = epoch2str(time);
}

async function clearDate(gate) {
    await ajaxGet(`/api/cleardate?g=${gate}`);
    notify("Schedule updated", "success");
    const schedule = document.getElementById(`schedule_${gate}`);
    schedule.innerHTML = "not set";
}