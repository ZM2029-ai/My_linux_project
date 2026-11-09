const $ = id => document.getElementById(id);
const statuses = {online: '在线', offline: '离线', unknown: '状态未知'};
const id = new URLSearchParams(location.search).get('device');
async function get(url) {
    const response = await fetch(url, {cache: 'no-store'});
    if (!response.ok) throw new Error(`请求失败（${response.status}）`);
    return response.json();
}
function text(tag, value) {
    const element = document.createElement(tag);
    element.textContent = value;
    return element;
}
function summary(device) {
    const latest = device.latest;
    return `${statuses[device.presence] || '状态未知'} · ${latest ? `温度 ${latest.temperature_c} °C，湿度 ${latest.humidity_percent} % · 采样 ${latest.sampled_at}` : '暂无遥测数据'}`;
}
async function refreshList() {
    try {
        const data = await get('/api/devices');
        $('devices').replaceChildren();
        for (const device of data.devices) {
            const card = document.createElement('article'); card.className = 'device';
            const link = text('a', device.display_name); link.href = `/?device=${encodeURIComponent(device.device_id)}`;
            card.append(link, text('p', device.device_id), text('p', summary(device)));
            $('devices').append(card);
        }
        $('list-message').textContent = data.devices.length ? '' : '暂无设备';
    } catch (error) { $('list-message').textContent = error.message; }
}
async function refreshDetail() {
    try {
        const device = await get(`/api/devices/${encodeURIComponent(id)}`);
        $('device-name').textContent = device.display_name;
        $('device-info').replaceChildren(text('p', `设备 ID：${device.device_id}`), text('p', summary(device)), text('p', `最近接收：${device.last_received_at}`));
        $('detail-message').textContent = '';
    } catch (error) { $('detail-message').textContent = error.message; }
}
async function sendControl(enabled) {
    const buttons = [$('pause-reporting'), $('resume-reporting')];
    const message = $('control-message');
    buttons.forEach(button => button.disabled = true);
    message.textContent = '正在发送控制命令…';
    try {
        const response = await fetch(`/api/devices/${encodeURIComponent(id)}/commands`, {
            method: 'POST', cache: 'no-store', headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({enabled})
        });
        const accepted = await response.json();
        if (!response.ok) throw new Error(accepted.error || `请求失败（${response.status}）`);
        if (response.status !== 202) throw new Error('网关没有接受控制命令');
        const deadline = Date.now() + 7000;
        let result = accepted;
        while (result.status === 'pending' && Date.now() < deadline) {
            await new Promise(resolve => setTimeout(resolve, 250));
            result = await get(`/api/devices/${encodeURIComponent(id)}/commands/${encodeURIComponent(accepted.command_id)}`);
        }
        message.textContent = result.status === 'pending' || result.status === 'timed_out'
            ? '未收到设备确认' : result.status === 'succeeded'
                ? (enabled ? '设备已恢复遥测' : '设备已暂停遥测')
                : `设备执行失败：${result.message}`;
        if (result.status === 'succeeded') refreshDetail();
    } catch (error) {
        message.textContent = error.message;
    } finally {
        buttons.forEach(button => button.disabled = false);
    }
}
function chart(title, samples, field, unit) {
    const container = document.createElement('div'); container.className = 'chart';
    container.append(text('h4', title));
    const ns = 'http://www.w3.org/2000/svg';
    const svg = document.createElementNS(ns, 'svg'); svg.setAttribute('viewBox', '0 0 800 220'); svg.setAttribute('role', 'img'); svg.setAttribute('aria-label', `${title}，共 ${samples.length} 条记录`);
    const values = samples.map(row => row[field]);
    const low = Math.min(...values), high = Math.max(...values), span = high - low || 1;
    const times = samples.map(row => Date.parse(row.sampled_at.replace(' ', 'T') + '+08:00'));
    const first = times[0], duration = times[times.length - 1] - first || 1;
    const line = document.createElementNS(ns, 'polyline');
    line.setAttribute('points', samples.map((row, i) => `${50 + 730 * (times[i] - first) / duration},${180 - 150 * (row[field] - low) / span}`).join(' ')); svg.append(line);
    for (const [x, y, value] of [[2, 30, `${high} ${unit}`], [2, 180, `${low} ${unit}`], [50, 210, samples[0].sampled_at], [580, 210, samples[samples.length - 1].sampled_at]]) {
        const label = document.createElementNS(ns, 'text'); label.setAttribute('x', x); label.setAttribute('y', y); label.textContent = value; svg.append(label);
    }
    if (samples.length === 1) { const dot = document.createElementNS(ns, 'circle'); dot.setAttribute('cx', '50'); dot.setAttribute('cy', '180'); dot.setAttribute('r', '4'); dot.setAttribute('fill', '#087b83'); svg.append(dot); }
    container.append(svg); return container;
}
function beijingInput(date) { return new Date(date.getTime() + 8 * 3600000).toISOString().slice(0, 19); }
if (id) {
    $('overview').hidden = true; $('detail').hidden = false;
    $('pause-reporting').addEventListener('click', () => sendControl(false));
    $('resume-reporting').addEventListener('click', () => sendControl(true));
    get('/api/session').then(session => { $('control-panel').hidden = !session.can_control; })
        .catch(error => { $('control-message').textContent = error.message; });
    const now = new Date(); $('end').value = beijingInput(now); $('start').value = beijingInput(new Date(now.getTime() - 86400000));
    refreshDetail(); setInterval(refreshDetail, 10000);
    $('history-form').addEventListener('submit', async event => {
        event.preventDefault(); $('history-message').textContent = '加载中…'; $('charts').replaceChildren();
        try {
            const start = $('start').value.replace('T', ' '); const end = $('end').value.replace('T', ' ');
            const params = new URLSearchParams({start: start.length === 16 ? start + ':00' : start, end: end.length === 16 ? end + ':00' : end, limit: '1000'});
            const data = await get(`/api/devices/${encodeURIComponent(id)}/telemetry?${params}`);
            if (!data.samples.length) { $('history-message').textContent = '这个时间范围内没有遥测记录'; return; }
            $('history-message').textContent = data.truncated ? '记录超过 1000 条，仅显示前 1000 条，请缩小查询范围。' : `共 ${data.samples.length} 条记录`;
            $('charts').append(chart('温度', data.samples, 'temperature_c', '°C'), chart('湿度', data.samples, 'humidity_percent', '%'));
        } catch (error) { $('history-message').textContent = error.message; }
    });
} else { refreshList(); setInterval(refreshList, 10000); }
