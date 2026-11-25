/**
 * ============================================================================
 * PRODUTO: SinoTech — Sistema Inteligente de Sinalização Escolar Inclusiva
 * ============================================================================
 */

#include <Arduino.h>
#include <Wire.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <WebServer.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <RTClib.h>
#include <ArduinoJson.h>
#include <DFRobotDFPlayerMini.h>
#include <time.h>
#include <sys/time.h>

// Proteção contra oscilações de tensão na fonte/cabos (Brownout)
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

// ============================================================================
// 1. DEFINIÇÃO DOS PINOS FÍSICOS
// ============================================================================
HardwareSerial mySoftwareSerial(2); // Pinos RX2=16 e TX2=17
DFRobotDFPlayerMini myDFPlayer;

// 5 LEDs: [0]=Vermelho1(25), [1]=Vermelho2(26), [2]=Verde(27), [3]=Azul(32), [4]=Laranja(33)
const int leds[] = {25, 26, 27, 32, 33};
const int numLeds = 5;

// Botão Físico Local
const int pinoBotao = 4;

// Barramento I²C do Relógio RTC
#define PIN_I2C_SDA 21
#define PIN_I2C_SCL 22

// Tipos de Sinalização Visual Inclusiva
#define VISUAL_CLASS_RED       1 // Vermelho (Aula / Sala)
#define VISUAL_BREAK_GREEN     2 // Verde (Intervalo / Recreio)
#define VISUAL_ALERT_ORANGE    3 // Laranja (Atenção / Aviso)
#define VISUAL_EMERGENCY_BLUE  4 // Azul (Emergência / Evacuar)

// Duração exata de cada som
uint16_t getTrackDurationSec(uint8_t track) {
  switch (track) {
    case 1:  return 8;
    case 2:  return 16;
    case 3:  return 8;
    case 4:  return 3;
    default: return 8;
  }
}

// ============================================================================
// 2. MINI-SERVIDOR DNS NATIVO (ABRE A PÁGINA AUTOMATICAMENTE AO LIGAR AO WI-FI)
// ============================================================================
WiFiUDP dnsUdp;

void startNativeCaptiveDns() {
  dnsUdp.begin(53);
}

void processNativeCaptiveDns() {
  int packetSize = dnsUdp.parsePacket();
  if (packetSize > 12 && packetSize < 256) {
    uint8_t buf[272];
    int len = dnsUdp.read(buf, 256);
    if (len <= 12) return;

    buf[2] = 0x81;
    buf[3] = 0x80;
    buf[6] = 0x00;
    buf[7] = 0x01;
    buf[8] = 0x00;
    buf[9] = 0x00;
    buf[10] = 0x00;
    buf[11] = 0x00;

    uint8_t ans[16] = {
      0xC0, 0x0C,
      0x00, 0x01,
      0x00, 0x01,
      0x00, 0x00, 0x00, 0x3C,
      0x00, 0x04,
      192, 168, 4, 1
    };
    memcpy(buf + len, ans, 16);

    dnsUdp.beginPacket(dnsUdp.remoteIP(), dnsUdp.remotePort());
    dnsUdp.write(buf, len + 16);
    dnsUdp.endPacket();
  }
}

// ============================================================================
// 3. APLICAÇÃO WEB EMBUTIDA (PREENCHIMENTO AUTOMÁTICO + LIBERDADE DE EDIÇÃO)
// ============================================================================
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-PT">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>SinoTech — Gestão de Sinalização Escolar Inclusiva</title>
  <style>
    :root {
      --bg-app: #f1f5f9;
      --bg-surface: #ffffff;
      --bg-sidebar: #0f172a;
      --bg-sidebar-hover: #1e293b;
      --text-primary: #0f172a;
      --text-secondary: #334155;
      --text-muted: #64748b;
      --border: #e2e8f0;
      --brand: #0284c7;
      --brand-dark: #0369a1;
      --success: #16a34a;
      --warning: #ea580c;
      --danger: #dc2626;
      --info: #2563eb;
      --radius: 8px;
      --shadow: 0 1px 3px rgba(15, 23, 42, 0.06);
    }

    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Inter, sans-serif;
      background: var(--bg-app);
      color: var(--text-primary);
      display: flex;
      min-height: 100vh;
    }

    .sidebar {
      width: 255px;
      background: var(--bg-sidebar);
      color: #f8fafc;
      display: flex;
      flex-direction: column;
      flex-shrink: 0;
    }
    .brand-header {
      padding: 20px;
      border-bottom: 1px solid #1e293b;
      display: flex;
      align-items: center;
      gap: 12px;
    }
    .brand-logo {
      width: 36px;
      height: 36px;
      border-radius: 7px;
      background: var(--brand);
      display: flex;
      align-items: center;
      justify-content: center;
      flex-shrink: 0;
    }
    .brand-logo svg { width: 20px; height: 20px; fill: #fff; }
    .brand-title { font-size: 16px; font-weight: 700; letter-spacing: -0.3px; }
    .brand-sub { font-size: 11px; color: #94a3b8; display: block; margin-top: 2px; }

    .nav-list { padding: 14px 10px; list-style: none; flex: 1; }
    .nav-item {
      display: flex;
      align-items: center;
      gap: 10px;
      padding: 10px 14px;
      border-radius: 6px;
      color: #cbd5e1;
      font-size: 13.5px;
      font-weight: 500;
      cursor: pointer;
      transition: all 0.15s ease;
      margin-bottom: 4px;
    }
    .nav-item:hover { background: var(--bg-sidebar-hover); color: #fff; }
    .nav-item.active { background: var(--brand); color: #fff; font-weight: 600; }
    .nav-item svg { width: 17px; height: 17px; fill: currentColor; flex-shrink: 0; }

    .sidebar-footer {
      padding: 16px;
      border-top: 1px solid #1e293b;
      font-size: 12px;
      color: #94a3b8;
      line-height: 1.5;
    }

    .main-area {
      flex: 1;
      display: flex;
      flex-direction: column;
      min-width: 0;
    }
    .topbar {
      background: var(--bg-surface);
      border-bottom: 1px solid var(--border);
      padding: 14px 24px;
      display: flex;
      align-items: center;
      justify-content: space-between;
      gap: 14px;
      flex-wrap: wrap;
    }
    .school-title { font-size: 17px; font-weight: 700; color: var(--text-primary); }
    .topbar-right { display: flex; align-items: center; gap: 8px; flex-wrap: wrap; }

    .status-pill {
      display: inline-flex;
      align-items: center;
      gap: 6px;
      padding: 5px 10px;
      border-radius: 6px;
      font-size: 12px;
      font-weight: 600;
      background: #f8fafc;
      border: 1px solid var(--border);
      color: var(--text-secondary);
      white-space: nowrap;
    }
    .status-pill.ok { border-left: 3px solid var(--success); }
    .status-pill.warn { border-left: 3px solid var(--warning); }
    .status-pill.err { border-left: 3px solid var(--danger); }

    .tag-signal {
      display: inline-flex;
      align-items: center;
      gap: 7px;
      padding: 5px 10px;
      border-radius: 6px;
      font-size: 12px;
      font-weight: 600;
      background: #f8fafc;
      border: 1px solid var(--border);
      color: var(--text-primary);
      white-space: nowrap;
    }
    .tag-signal svg { width: 14px; height: 14px; flex-shrink: 0; }
    .tag-signal.sig-red { border-left: 3px solid var(--danger); }
    .tag-signal.sig-red svg { fill: var(--danger); }
    .tag-signal.sig-green { border-left: 3px solid var(--success); }
    .tag-signal.sig-green svg { fill: var(--success); }
    .tag-signal.sig-orange { border-left: 3px solid var(--warning); }
    .tag-signal.sig-orange svg { fill: var(--warning); }
    .tag-signal.sig-blue { border-left: 3px solid var(--info); }
    .tag-signal.sig-blue svg { fill: var(--info); }

    .content { padding: 22px 24px; max-width: 1360px; width: 100%; margin: 0 auto; }
    .view-section { display: none; }
    .view-section.active { display: block; }

    .grid-4 { display: grid; grid-template-columns: repeat(auto-fit, minmax(230px, 1fr)); gap: 16px; margin-bottom: 20px; }
    .grid-2 { display: grid; grid-template-columns: repeat(auto-fit, minmax(350px, 1fr)); gap: 20px; margin-bottom: 20px; }

    .card {
      background: var(--bg-surface);
      border: 1px solid var(--border);
      border-radius: var(--radius);
      padding: 18px 20px;
      box-shadow: var(--shadow);
    }
    .card-header {
      display: flex;
      align-items: center;
      justify-content: space-between;
      margin-bottom: 14px;
      padding-bottom: 10px;
      border-bottom: 1px solid var(--border);
      gap: 10px;
      flex-wrap: wrap;
    }
    .card-title { font-size: 12.5px; font-weight: 700; text-transform: uppercase; letter-spacing: 0.4px; color: var(--text-secondary); }
    .metric-val { font-size: 27px; font-weight: 800; color: var(--text-primary); letter-spacing: -0.5px; margin: 6px 0; }
    .metric-sub { font-size: 13px; color: var(--text-muted); }

    .led-panel {
      background: #0f172a;
      border-radius: 8px;
      padding: 16px;
      color: #f8fafc;
      display: flex;
      flex-direction: column;
      gap: 14px;
    }
    .led-strip {
      display: flex;
      justify-content: space-around;
      align-items: center;
      padding: 14px 8px;
      background: #1e293b;
      border-radius: 6px;
    }
    .led-node {
      display: flex;
      flex-direction: column;
      align-items: center;
      gap: 6px;
      text-align: center;
    }
    .led-bulb {
      width: 28px;
      height: 28px;
      border-radius: 50%;
      border: 2px solid rgba(255,255,255,0.18);
      transition: all 0.12s ease;
    }
    .led-bulb.red-off { background: #450a0a; }
    .led-bulb.red-on {
      background: #ef4444;
      box-shadow: 0 0 16px #ef4444;
      border-color: #fca5a5;
    }
    .led-bulb.green-off { background: #052e16; }
    .led-bulb.green-on {
      background: #22c55e;
      box-shadow: 0 0 16px #22c55e;
      border-color: #86efac;
    }
    .led-bulb.blue-off { background: #172554; }
    .led-bulb.blue-on {
      background: #3b82f6;
      box-shadow: 0 0 16px #3b82f6;
      border-color: #93c5fd;
    }
    .led-bulb.orange-off { background: #431407; }
    .led-bulb.orange-on {
      background: #f97316;
      box-shadow: 0 0 16px #f97316;
      border-color: #fdba74;
    }
    .led-label { font-size: 11px; color: #e2e8f0; font-weight: 600; }
    .led-sub { font-size: 10px; color: #94a3b8; }

    table { width: 100%; border-collapse: collapse; font-size: 13.5px; }
    th, td { padding: 11px 12px; text-align: left; border-bottom: 1px solid var(--border); vertical-align: middle; }
    th { font-size: 11px; text-transform: uppercase; color: var(--text-muted); font-weight: 700; background: #f8fafc; }
    tr:hover td { background: #f8fafc; }

    .btn {
      display: inline-flex;
      align-items: center;
      justify-content: center;
      gap: 6px;
      padding: 8px 14px;
      border-radius: 6px;
      font-size: 13px;
      font-weight: 600;
      border: 1px solid transparent;
      cursor: pointer;
      transition: all 0.15s;
    }
    .btn svg { width: 14px; height: 14px; fill: currentColor; flex-shrink: 0; }
    .btn-primary { background: var(--brand); color: #fff; }
    .btn-primary:hover { background: var(--brand-dark); }
    .btn-danger { background: #fff; border-color: #fecaca; color: var(--danger); }
    .btn-danger:hover { background: #fef2f2; }
    .btn-danger-solid { background: var(--danger); color: #fff; }
    .btn-outline { background: #fff; border-color: var(--border); color: var(--text-primary); }
    .btn-outline:hover { background: #f1f5f9; }
    .btn-sm { padding: 5px 10px; font-size: 12px; }

    .form-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 14px; margin-bottom: 16px; }
    .form-group { display: flex; flex-direction: column; gap: 6px; }
    .form-group label { font-size: 12px; font-weight: 600; color: var(--text-secondary); }
    .form-control {
      padding: 9px 11px;
      border: 1px solid var(--border);
      border-radius: 6px;
      font-size: 13.5px;
      outline: none;
      background: #fff;
    }
    .form-control:focus { border-color: var(--brand); }

    .days-selector { display: flex; gap: 6px; flex-wrap: wrap; }
    .day-chip {
      padding: 6px 12px;
      border-radius: 6px;
      border: 1px solid var(--border);
      font-size: 12px;
      font-weight: 600;
      cursor: pointer;
      user-select: none;
      background: #fff;
    }
    .day-chip.selected { background: var(--brand); color: #fff; border-color: var(--brand); }

    .week-grid {
      display: grid;
      grid-template-columns: repeat(7, minmax(130px, 1fr));
      gap: 10px;
      overflow-x: auto;
    }
    .day-col {
      background: #f8fafc;
      border: 1px solid var(--border);
      border-radius: 6px;
      padding: 10px;
      min-height: 220px;
    }
    .day-col-header {
      font-size: 12.5px;
      font-weight: 700;
      text-align: center;
      padding-bottom: 8px;
      margin-bottom: 8px;
      border-bottom: 2px solid var(--border);
    }
    .cal-event {
      background: #fff;
      border: 1px solid var(--border);
      border-left: 3px solid var(--brand);
      border-radius: 5px;
      padding: 7px 8px;
      margin-bottom: 7px;
      font-size: 12px;
    }
    .cal-event.v-red { border-left-color: var(--danger); }
    .cal-event.v-green { border-left-color: var(--success); }
    .cal-event.v-orange { border-left-color: var(--warning); }
    .cal-event.v-blue { border-left-color: var(--info); }
    .cal-event.inactive { border-left-color: #94a3b8; opacity: 0.55; }

    .toast-container {
      position: fixed;
      bottom: 22px;
      right: 22px;
      z-index: 9999;
      display: flex;
      flex-direction: column;
      gap: 10px;
      max-width: 410px;
    }
    .toast {
      background: #0f172a;
      color: #fff;
      padding: 14px 16px;
      border-radius: 8px;
      box-shadow: 0 10px 25px rgba(0,0,0,0.22);
      border-left: 4px solid var(--brand);
    }
    .toast.warn { border-left-color: var(--warning); }
    .toast.ok { border-left-color: var(--success); }
    .toast.err { border-left-color: var(--danger); }
    .toast-title { font-size: 13.5px; font-weight: 700; margin-bottom: 4px; display: flex; justify-content: space-between; gap: 12px; }
    .toast-msg { font-size: 12.5px; color: #cbd5e1; line-height: 1.45; }

    @media (max-width: 860px) {
      body { flex-direction: column; }
      .sidebar { width: 100%; }
      .nav-list { display: flex; overflow-x: auto; padding: 8px 10px; gap: 6px; }
      .nav-item { margin-bottom: 0; white-space: nowrap; }
      .sidebar-footer { display: none; }
      .content { padding: 14px; }
      .topbar { padding: 12px 14px; }
    }
  </style>
</head>
<body>

  <aside class="sidebar">
    <div class="brand-header">
      <div class="brand-logo">
        <svg viewBox="0 0 24 24"><path d="M12 22a2 2 0 0 0 2-2h-4a2 2 0 0 0 2 2zm6-6v-5c0-3.07-1.64-5.64-4.5-6.32V4c0-.83-.67-1.5-1.5-1.5s-1.5.67-1.5 1.5v.68C7.63 5.36 6 7.92 6 11v5l-2 2v1h16v-1l-2-2z"/></svg>
      </div>
      <div>
        <div class="brand-title">SinoTech</div>
        <span class="brand-sub">Sinalização Escolar Inclusiva</span>
      </div>
    </div>

    <ul class="nav-list">
      <li class="nav-item active" onclick="switchTab('dashboard', this)">
        <svg viewBox="0 0 24 24"><path d="M3 13h8V3H3v10zm0 8h8v-6H3v6zm10 0h8V11h-8v10zm0-18v6h8V3h-8z"/></svg>
        Painel Principal
      </li>
      <li class="nav-item" onclick="switchTab('schedules', this)">
        <svg viewBox="0 0 24 24"><path d="M11.99 2C6.47 2 2 6.48 2 12s4.47 10 9.99 10C17.52 22 22 17.52 22 12S17.52 2 11.99 2zM12 20c-4.42 0-8-3.58-8-8s3.58-8 8-8 8 3.58 8 8-3.58 8-8 8zm.5-13H11v6l5.25 3.15.75-1.23-4.5-2.67z"/></svg>
        Horários Escolares
      </li>
      <li class="nav-item" onclick="switchTab('calendar', this)">
        <svg viewBox="0 0 24 24"><path d="M19 4h-1V2h-2v2H8V2H6v2H5c-1.11 0-1.99.9-1.99 2L3 20a2 2 0 0 0 2 2h14c1.1 0 2-.9 2-2V6c0-1.1-.9-2-2-2zm0 16H5V10h14v10z"/></svg>
        Calendário Semanal
      </li>
      <li class="nav-item" onclick="switchTab('manual', this)">
        <svg viewBox="0 0 24 24"><path d="M12 3v10.55c-.59-.34-1.27-.55-2-.55-2.21 0-4 1.79-4 4s1.79 4 4 4 4-1.79 4-4V7h4V3h-6z"/></svg>
        Toque Manual
      </li>
      <li class="nav-item" onclick="switchTab('history', this)">
        <svg viewBox="0 0 24 24"><path d="M13 3a9 9 0 0 0-9 9H1l3.89 3.89.07.14L9 12H6c0-3.87 3.13-7 7-7s7 3.13 7 7-3.13 7-7 7c-1.93 0-3.68-.79-4.94-2.06l-1.42 1.42A8.954 8.954 0 0 0 13 21a9 9 0 0 0 0-18zm-1 5v5l4.28 2.54.72-1.21-3.5-2.08V8H12z"/></svg>
        Registo de Toques
      </li>
      <li class="nav-item" onclick="switchTab('settings', this)">
        <svg viewBox="0 0 24 24"><path d="M19.14 12.94c.04-.3.06-.61.06-.94 0-.32-.02-.64-.07-.94l2.03-1.58a.49.49 0 0 0 .12-.61l-1.92-3.32a.488.488 0 0 0-.59-.22l-2.39.96c-.5-.38-1.03-.7-1.62-.94l-.36-2.54a.484.484 0 0 0-.48-.41h-3.84c-.24 0-.43.17-.47.41l-.36 2.54c-.59.24-1.13.57-1.62.94l-2.39-.96c-.22-.08-.47 0-.59.22L2.74 8.87c-.12.21-.08.47.12.61l2.03 1.58c-.05.3-.09.63-.09.94s.02.64.07.94l-2.03 1.58a.49.49 0 0 0-.12.61l1.92 3.32c.12.22.37.29.59.22l2.39-.96c.5.38 1.03.7 1.62.94l.36 2.54c.05.24.24.41.48.41h3.84c.24 0 .44-.17.47-.41l.36-2.54c.59-.24 1.13-.56 1.62-.94l2.39.96c.22.08.47 0 .59-.22l1.92-3.32c.12-.22.07-.47-.12-.61l-2.01-1.58zM12 15.6c-1.98 0-3.6-1.62-3.6-3.6s1.62-3.6 3.6-3.6 3.6 1.62 3.6 3.6-1.62 3.6-3.6 3.6z"/></svg>
        Configurações
      </li>
    </ul>

    <div class="sidebar-footer">
      Horário Oficial de Angola<br>
      Painel de Gestão Escolar
    </div>
  </aside>

  <div class="main-area">
    <header class="topbar">
      <div class="school-title" id="uiSchoolName">Complexo Escolar SinoTech</div>
      <div class="topbar-right">
        <span class="status-pill ok" id="badgeEsp">Central Ligada</span>
        <span class="status-pill ok" id="badgeRtc">Relógio: Automático</span>
        <span class="status-pill ok" id="badgeAudio">Som: Pronto</span>
      </div>
    </header>

    <main class="content">
      <section id="tab-dashboard" class="view-section active">
        <div class="grid-4">
          <div class="card">
            <div class="card-header"><span class="card-title">Hora Atual</span></div>
            <div class="metric-val" id="dashTime">--:--:--</div>
            <div class="metric-sub" id="dashDate">A atualizar...</div>
          </div>

          <div class="card">
            <div class="card-header"><span class="card-title">Próximo Toque</span></div>
            <div class="metric-val" id="dashNextTime">--:--</div>
            <div class="metric-sub" id="dashNextDesc">A verificar agenda...</div>
          </div>

          <div class="card">
            <div class="card-header"><span class="card-title">Toques de Hoje</span></div>
            <div class="metric-val" id="dashTodayCount">0</div>
            <div class="metric-sub"><span id="dashTotalActive">0</span> horários ativos na semana</div>
          </div>

          <div class="card">
            <div class="card-header"><span class="card-title">Estado da Campainha</span></div>
            <div class="metric-val" id="dashBellState">Em Silêncio</div>
            <div class="metric-sub" id="dashLastEvent">Último: --:--</div>
          </div>
        </div>

        <div class="grid-2">
          <div class="card">
            <div class="card-header">
              <span class="card-title">Painel Luminoso Inclusivo (Em Tempo Real)</span>
            </div>
            <div class="led-panel">
              <div class="led-strip">
                <div class="led-node">
                  <div id="led0" class="led-bulb red-off"></div>
                  <span class="led-label">Vermelho</span>
                  <span class="led-sub">Aula / Sala</span>
                </div>
                <div class="led-node">
                  <div id="led1" class="led-bulb red-off"></div>
                  <span class="led-label">Vermelho</span>
                  <span class="led-sub">Aula / Sala</span>
                </div>
                <div class="led-node">
                  <div id="led2" class="led-bulb green-off"></div>
                  <span class="led-label">Verde</span>
                  <span class="led-sub">Intervalo</span>
                </div>
                <div class="led-node">
                  <div id="led3" class="led-bulb blue-off"></div>
                  <span class="led-label">Azul</span>
                  <span class="led-sub">Emergência</span>
                </div>
                <div class="led-node">
                  <div id="led4" class="led-bulb orange-off"></div>
                  <span class="led-label">Laranja</span>
                  <span class="led-sub">Atenção</span>
                </div>
              </div>
              <div style="display:flex; justify-content:space-between; font-size:12.5px; color:#cbd5e1; flex-wrap:wrap; gap:8px;">
                <div><strong>Sinal Ativo:</strong> <span id="dashLedPattern">-</span></div>
                <div><strong>Situação:</strong> <span id="dashLedReason">-</span></div>
              </div>
            </div>
          </div>

          <div class="card">
            <div class="card-header">
              <span class="card-title">Ações Rápidas de Sinalização</span>
            </div>
            <table>
              <tbody>
                <tr>
                  <td><strong>Sincronização da Hora</strong></td>
                  <td id="diagTimeSource">-</td>
                </tr>
                <tr>
                  <td><strong>Duração dos Toques</strong></td>
                  <td>Toque 1: 8s · Toque 2: 16s · Toque 3: 8s · Toque 4: 3s</td>
                </tr>
                <tr>
                  <td><strong>Acionamento Rápido</strong></td>
                  <td style="display:flex; gap:8px; flex-wrap:wrap;">
                    <button class="btn btn-outline btn-sm" onclick="quickVisualRing(1, 1, 'Início ou Fim de Aula / Troca de Turno')">Aula / Sala</button>
                    <button class="btn btn-outline btn-sm" onclick="quickVisualRing(2, 2, 'Intervalo / Recreio / Saída Livre')">Intervalo</button>
                    <button class="btn btn-outline btn-sm" onclick="quickVisualRing(3, 3, 'Atenção / Aviso Prévio')">Atenção</button>
                    <button class="btn btn-outline btn-sm" onclick="quickVisualRing(4, 4, 'Emergência / Evacuação')">Emergência</button>
                  </td>
                </tr>
                <tr>
                  <td><strong>Relógio da Escola</strong></td>
                  <td>
                    <button class="btn btn-primary btn-sm" onclick="syncBrowserTime()">Acertar Hora pelo Telemóvel / PC</button>
                  </td>
                </tr>
              </tbody>
            </table>
          </div>
        </div>
      </section>

      <section id="tab-schedules" class="view-section">
        <div class="card" style="margin-bottom:20px;">
          <div class="card-header">
            <span class="card-title" id="formSchedTitle">Programar Novo Horário Escolar</span>
            <button class="btn btn-outline btn-sm" onclick="resetScheduleForm()">Limpar Campos</button>
          </div>
          <input type="hidden" id="schedId" value="0">
          <div class="form-grid">
            <div class="form-group">
              <label>Hora (00–23)</label>
              <input type="number" id="schedHour" class="form-control" min="0" max="23" value="7">
            </div>
            <div class="form-group">
              <label>Minuto (00–59)</label>
              <input type="number" id="schedMin" class="form-control" min="0" max="59" value="30">
            </div>
            <div class="form-group">
              <label>Nome / Motivo do Toque</label>
              <input type="text" id="schedDesc" class="form-control" placeholder="Ex: Entrada dos Alunos">
            </div>
            <div class="form-group">
              <label>Sinal Luminoso Inclusivo (Cor)</label>
              <select id="schedVisual" class="form-control" onchange="autoSelectTrackAndDuration('schedVisual', 'schedTrack', 'schedDur')">
                <option value="1">Vermelho — Início/Fim de Aula ou Turno</option>
                <option value="2">Verde — Intervalo / Recreio / Saída Livre</option>
                <option value="3">Laranja — Atenção / Aviso Prévio</option>
                <option value="4">Azul — Emergência / Evacuar</option>
              </select>
            </div>
            <div class="form-group">
              <label>Som do Toque (Pode alterar se desejar)</label>
              <select id="schedTrack" class="form-control" onchange="autoAdjustDuration('schedTrack', 'schedDur')">
                <option value="1">Toque 1 — Principal (8s)</option>
                <option value="2">Toque 2 — Intervalo (16s)</option>
                <option value="3">Toque 3 — Aviso / Atenção (8s)</option>
                <option value="4">Toque 4 — Sirene / Emergência (3s)</option>
              </select>
            </div>
            <div class="form-group">
              <label>Duração do Toque (Segundos)</label>
              <input type="number" id="schedDur" class="form-control" min="1" max="60" value="8">
            </div>
          </div>

          <div class="form-group" style="margin-bottom:16px;">
            <label>Dias da Semana em que Deve Tocar</label>
            <div class="days-selector" id="daysSelector">
              <span class="day-chip" data-bit="0" onclick="toggleDayChip(this)">Dom</span>
              <span class="day-chip selected" data-bit="1" onclick="toggleDayChip(this)">Seg</span>
              <span class="day-chip selected" data-bit="2" onclick="toggleDayChip(this)">Ter</span>
              <span class="day-chip selected" data-bit="3" onclick="toggleDayChip(this)">Qua</span>
              <span class="day-chip selected" data-bit="4" onclick="toggleDayChip(this)">Qui</span>
              <span class="day-chip selected" data-bit="5" onclick="toggleDayChip(this)">Sex</span>
              <span class="day-chip" data-bit="6" onclick="toggleDayChip(this)">Sáb</span>
            </div>
          </div>

          <button class="btn btn-primary" onclick="saveSchedule()">Guardar Horário</button>
        </div>

        <div class="card">
          <div class="card-header">
            <span class="card-title">Horários Escolares Programados</span>
          </div>
          <div style="overflow-x:auto;">
            <table>
              <thead>
                <tr>
                  <th>Estado</th>
                  <th>Hora</th>
                  <th>Evento Escolar</th>
                  <th>Sinal Luminoso</th>
                  <th>Dias</th>
                  <th>Som / Duração</th>
                  <th>Opções</th>
                </tr>
              </thead>
              <tbody id="schedulesTableBody"></tbody>
            </table>
          </div>
        </div>
      </section>

      <section id="tab-calendar" class="view-section">
        <div class="card">
          <div class="card-header">
            <span class="card-title">Grade Semanal de Toques</span>
          </div>
          <div class="week-grid" id="calendarGrid"></div>
        </div>
      </section>

      <section id="tab-manual" class="view-section">
        <div class="grid-2">
          <div class="card">
            <div class="card-header"><span class="card-title">Acionar Campainha e Sinal Luminoso Manualmente</span></div>
            <p style="font-size:13.5px; color:var(--text-secondary); margin-bottom:16px;">
              Ao escolher o sinal luminoso, o sistema seleciona automaticamente o som e a duração recomendados, mantendo a liberdade para os alterar manualmente se desejar.
            </p>
            <div class="form-grid">
              <div class="form-group">
                <label>Sinal Luminoso Inclusivo (Cor)</label>
                <select id="manVisual" class="form-control" onchange="autoSelectTrackAndDuration('manVisual', 'manTrack', 'manDuration')">
                  <option value="3">Laranja — Atenção / Aviso</option>
                  <option value="1">Vermelho — Aula / Voltar para a sala</option>
                  <option value="2">Verde — Intervalo / Recreio</option>
                  <option value="4">Azul — Emergência / Evacuar</option>
                </select>
              </div>
              <div class="form-group">
                <label>Som a Reproduzir (Pode alterar se desejar)</label>
                <select id="manTrack" class="form-control" onchange="autoAdjustDuration('manTrack', 'manDuration')">
                  <option value="1">Toque 1 — Principal (8s)</option>
                  <option value="2">Toque 2 — Intervalo (16s)</option>
                  <option value="3" selected>Toque 3 — Aviso / Atenção (8s)</option>
                  <option value="4">Toque 4 — Sirene / Emergência (3s)</option>
                </select>
              </div>
              <div class="form-group">
                <label>Duração (Segundos)</label>
                <input type="number" id="manDuration" class="form-control" min="1" max="60" value="8">
              </div>
            </div>
            <div class="form-group" style="margin-bottom:16px;">
              <label>Motivo do Toque</label>
              <input type="text" id="manReason" class="form-control" value="Aviso Geral / Sinalização Manual">
            </div>
            <div style="display:flex; gap:12px; flex-wrap:wrap;">
              <button class="btn btn-primary" onclick="manualRingConfirm()">Acionar Sinal Agora</button>
              <button class="btn btn-danger-solid" onclick="manualStopBell()">Parar Imediatamente</button>
            </div>
          </div>

          <div class="card">
            <div class="card-header"><span class="card-title">Significado das Cores de Sinalização</span></div>
            <table>
              <tbody>
                <tr>
                  <td><strong>Vermelho</strong></td>
                  <td><strong>Início ou fim de aula, troca de turno</strong><br>Indica entrada ou regresso à sala de aula (Sugere Toque 1 · 8s).</td>
                </tr>
                <tr>
                  <td><strong>Verde</strong></td>
                  <td><strong>Intervalo, recreio, saída livre</strong><br>Luz fixa indicando tempo livre ou recreio (Sugere Toque 2 · 16s).</td>
                </tr>
                <tr>
                  <td><strong>Azul</strong></td>
                  <td><strong>Emergência, evacuação</strong><br>Sinal rápido indicando saída imediata do edifício (Sugere Toque 4 · 3s).</td>
                </tr>
                <tr>
                  <td><strong>Laranja</strong></td>
                  <td><strong>Atenção, aviso prévio</strong><br>Indica aviso geral ou atenção (Sugere Toque 3 · 8s).</td>
                </tr>
              </tbody>
            </table>
          </div>
        </div>
      </section>

      <section id="tab-history" class="view-section">
        <div class="card">
          <div class="card-header">
            <span class="card-title">Registo de Últimos Toques Realizados</span>
            <button class="btn btn-outline btn-sm" onclick="loadHistory()">Atualizar</button>
          </div>
          <div style="overflow-x:auto;">
            <table>
              <thead>
                <tr>
                  <th>Data</th>
                  <th>Previsto</th>
                  <th>Hora Real</th>
                  <th>Descrição</th>
                  <th>Origem</th>
                  <th>Sinal Luminoso</th>
                </tr>
              </thead>
              <tbody id="historyTableBody"></tbody>
            </table>
          </div>
        </div>
      </section>

      <section id="tab-settings" class="view-section">
        <div class="grid-2">
          <div class="card">
            <div class="card-header"><span class="card-title">Acerto da Data e Hora da Escola</span></div>
            <div class="form-grid">
              <div class="form-group">
                <label>Data</label>
                <input type="date" id="cfgDate" class="form-control">
              </div>
              <div class="form-group">
                <label>Hora</label>
                <input type="time" id="cfgTime" step="1" class="form-control">
              </div>
            </div>
            <div style="display:flex; gap:10px; flex-wrap:wrap;">
              <button class="btn btn-primary" onclick="syncBrowserTime()">Copiar Hora Certa do Telemóvel / PC</button>
              <button class="btn btn-outline" onclick="setManualClock()">Guardar Data e Hora Manual</button>
            </div>
          </div>

          <div class="card">
            <div class="card-header"><span class="card-title">Preferências da Escola, Botão Local, Volume e Wi-Fi</span></div>
            <div class="form-grid">
              <div class="form-group">
                <label>Nome da Instituição de Ensino</label>
                <input type="text" id="cfgSchool" class="form-control">
              </div>
              <div class="form-group">
                <label>Volume da Campainha (0 a 30)</label>
                <input type="number" id="cfgVolume" class="form-control" min="0" max="30" value="25">
              </div>
              <div class="form-group">
                <label>Cor Acionada pelo Botão Físico da Caixa</label>
                <select id="cfgBtnVisual" class="form-control">
                  <option value="3">Laranja (Atenção / Toque Manual)</option>
                  <option value="1">Vermelho (Aula / Voltar à Sala)</option>
                  <option value="2">Verde (Intervalo / Recreio)</option>
                  <option value="4">Azul (Emergência / Evacuar)</option>
                </select>
              </div>
              <div class="form-group">
                <label>Nome do Wi-Fi da Campainha</label>
                <input type="text" id="cfgApSsid" class="form-control">
              </div>
              <div class="form-group">
                <label>Nova Senha do Wi-Fi (Mín. 8 caract.)</label>
                <input type="password" id="cfgApPass" class="form-control" placeholder="Em branco mantém a senha atual">
              </div>
            </div>
            <div style="display:flex; justify-content:space-between; margin-top:10px; flex-wrap:wrap; gap:10px;">
              <button class="btn btn-primary" onclick="saveConfig()">Guardar Alterações</button>
              <button class="btn btn-danger" onclick="rebootDevice()">Reiniciar Equipamento</button>
            </div>
          </div>
        </div>
      </section>
    </main>
  </div>

  <div class="toast-container" id="toastContainer"></div>

  <script>
    const DAY_NAMES = ['Dom', 'Seg', 'Ter', 'Qua', 'Qui', 'Sex', 'Sáb'];
    const FULL_DAYS = ['Domingo', 'Segunda-feira', 'Terça-feira', 'Quarta-feira', 'Quinta-feira', 'Sexta-feira', 'Sábado'];
    const TRACK_DURATIONS = { 1: 8, 2: 16, 3: 8, 4: 3 };
    const VISUAL_DEFAULT_TRACK = { 1: 1, 2: 2, 3: 3, 4: 4 };
    let schedulesCache = [];
    let lastKnownRtcOnline = null;

    const SVG_CLASS = '<svg viewBox="0 0 24 24"><path d="M12 3L1 9l11 6 9-4.91V17h2V9M5 13.18v4L12 21l7-3.82v-4L12 17l-7-3.82z"/></svg>';
    const SVG_BREAK = '<svg viewBox="0 0 24 24"><path d="M12 2C6.48 2 2 6.48 2 12s4.48 10 10 10 10-4.48 10-10S17.52 2 12 2zm-2 14.5v-9l6 4.5-6 4.5z"/></svg>';
    const SVG_ALERT = '<svg viewBox="0 0 24 24"><path d="M12 22a2 2 0 0 0 2-2h-4a2 2 0 0 0 2 2zm6-6v-5c0-3.07-1.64-5.64-4.5-6.32V4c0-.83-.67-1.5-1.5-1.5s-1.5.67-1.5 1.5v.68C7.63 5.36 6 7.92 6 11v5l-2 2v1h16v-1l-2-2z"/></svg>';
    const SVG_EMERG = '<svg viewBox="0 0 24 24"><path d="M1 21h22L12 2 1 21zm12-3h-2v-2h2v2zm0-4h-2v-4h2v4z"/></svg>';

    // Quando o utilizador escolhe o Sinal Luminoso (Cor), preenche automaticamente o Som e a Duração sugeridos,
    // mas deixa os campos livres para ele alterar se quiser!
    function autoSelectTrackAndDuration(visualSelectId, trackSelectId, durInputId) {
      const vType = parseInt(document.getElementById(visualSelectId).value) || 1;
      const suggestedTrack = VISUAL_DEFAULT_TRACK[vType] || 1;
      document.getElementById(trackSelectId).value = suggestedTrack;
      document.getElementById(durInputId).value = TRACK_DURATIONS[suggestedTrack] || 8;
    }

    // Quando o utilizador muda manualmente o Som, ajusta a duração desse som (e deixa editar a seguir se quiser)
    function autoAdjustDuration(trackSelectId, durInputId) {
      const trk = parseInt(document.getElementById(trackSelectId).value) || 1;
      document.getElementById(durInputId).value = TRACK_DURATIONS[trk] || 8;
    }

    function visualTagHtml(vType) {
      if (vType === 2) return `<span class="tag-signal sig-green">${SVG_BREAK}Verde (Intervalo)</span>`;
      if (vType === 3) return `<span class="tag-signal sig-orange">${SVG_ALERT}Laranja (Atenção)</span>`;
      if (vType === 4) return `<span class="tag-signal sig-blue">${SVG_EMERG}Azul (Emergência)</span>`;
      return `<span class="tag-signal sig-red">${SVG_CLASS}Vermelho (Aula / Sala)</span>`;
    }

    function historyTagHtml(statusMsg, ok) {
      const s = String(statusMsg);
      if (s.indexOf('Verde') >= 0)    return `<span class="tag-signal sig-green">${SVG_BREAK}${s}</span>`;
      if (s.indexOf('Laranja') >= 0 || s.indexOf('Pronto') >= 0) return `<span class="tag-signal sig-orange">${SVG_ALERT}${s}</span>`;
      if (s.indexOf('Azul') >= 0)     return `<span class="tag-signal sig-blue">${SVG_EMERG}${s}</span>`;
      if (s.indexOf('Vermelho') >= 0) return `<span class="tag-signal sig-red">${SVG_CLASS}${s}</span>`;
      return `<span class="status-pill ${ok ? 'ok' : 'warn'}">${s}</span>`;
    }

    function visualCalClass(vType) {
      if (vType === 2) return 'v-green';
      if (vType === 3) return 'v-orange';
      if (vType === 4) return 'v-blue';
      return 'v-red';
    }

    function showToast(title, msg, type = 'ok', duration = 6000) {
      const container = document.getElementById('toastContainer');
      const el = document.createElement('div');
      el.className = 'toast ' + type;
      el.innerHTML = `<div class="toast-title"><span>${title}</span><span style="cursor:pointer" onclick="this.parentElement.parentElement.remove()">✕</span></div><div class="toast-msg">${msg}</div>`;
      container.appendChild(el);
      if (duration > 0) setTimeout(() => { if (el.parentElement) el.remove(); }, duration);
    }

    function switchTab(tabId, navEl) {
      document.querySelectorAll('.view-section').forEach(s => s.classList.remove('active'));
      document.querySelectorAll('.nav-item').forEach(n => n.classList.remove('active'));
      document.getElementById('tab-' + tabId).classList.add('active');
      if (navEl) navEl.classList.add('active');
      if (tabId === 'schedules' || tabId === 'calendar') loadSchedules();
      if (tabId === 'history') loadHistory();
      if (tabId === 'settings') loadConfig();
    }

    async function pollStatus() {
      try {
        const res = await fetch('/api/status');
        const d = await res.json();

        const bEsp = document.getElementById('badgeEsp');
        bEsp.className = 'status-pill ok';
        bEsp.textContent = 'Central Ligada';

        document.getElementById('uiSchoolName').textContent = d.schoolName || 'Complexo Escolar SinoTech';
        document.getElementById('dashTime').textContent = d.time;
        const wName = (d.weekday >= 0 && d.weekday <= 6) ? FULL_DAYS[d.weekday] : '';
        document.getElementById('dashDate').textContent = wName ? `${wName}, ${d.date}` : d.date;

        document.getElementById('dashNextTime').textContent = d.nextEventTime;
        document.getElementById('dashNextDesc').textContent = d.nextEventDesc;
        document.getElementById('dashTodayCount').textContent = d.todayEventsCount;
        document.getElementById('dashTotalActive').textContent = d.totalActiveSchedules;

        const bellEl = document.getElementById('dashBellState');
        if (d.bellActive) {
          bellEl.textContent = `A TOCAR (${d.bellRemainingSec}s)`;
          bellEl.style.color = 'var(--danger)';
        } else {
          bellEl.textContent = 'Em Silêncio';
          bellEl.style.color = 'var(--text-primary)';
        }
        document.getElementById('dashLastEvent').textContent = `Último: ${d.lastEventTime} (${d.lastEventDesc})`;

        const bRtc = document.getElementById('badgeRtc');
        if (d.rtcOnline) {
          bRtc.className = 'status-pill ok';
          bRtc.textContent = 'Relógio: Automático';
        } else {
          bRtc.className = 'status-pill warn';
          bRtc.textContent = 'Relógio: Modo Reserva';
        }

        const bAud = document.getElementById('badgeAudio');
        bAud.className = 'status-pill ok';
        bAud.textContent = 'Som: Pronto';

        document.getElementById('diagTimeSource').textContent = d.rtcOnline ? 'Relógio Principal Ativo' : 'Relógio Interno de Reserva';

        if (d.leds && d.leds.states) {
          const colors = ['red', 'red', 'green', 'blue', 'orange'];
          for (let i = 0; i < 5; i++) {
            const bulb = document.getElementById('led' + i);
            bulb.className = 'led-bulb ' + colors[i] + (d.leds.states[i] ? '-on' : '-off');
          }
          document.getElementById('dashLedPattern').textContent = d.leds.pattern;
          document.getElementById('dashLedReason').textContent = d.leds.reason;
        }

        const storedSeq = sessionStorage.getItem('rtcSeq');
        if (!d.rtcOnline && storedSeq !== String(d.rtcStateSeq)) {
          sessionStorage.setItem('rtcSeq', String(d.rtcStateSeq));
          showToast(
            'Aviso do Relógio',
            'O relógio principal entrou em modo de reserva automática. Os toques continuarão a funcionar normalmente.',
            'warn',
            12000
          );
        } else if (d.rtcOnline && lastKnownRtcOnline === false && storedSeq !== String(d.rtcStateSeq)) {
          sessionStorage.setItem('rtcSeq', String(d.rtcStateSeq));
          showToast(
            'Relógio Restabelecido',
            'O relógio principal voltou ao modo normal automaticamente.',
            'ok',
            9000
          );
        }
        lastKnownRtcOnline = d.rtcOnline;

      } catch (e) {
        const bEsp = document.getElementById('badgeEsp');
        bEsp.className = 'status-pill err';
        bEsp.textContent = 'Sem Ligação';
      }
    }

    function toggleDayChip(el) { el.classList.toggle('selected'); }

    function formatDaysMask(mask) {
      let out = [];
      for (let i = 0; i < 7; i++) if (mask & (1 << i)) out.push(DAY_NAMES[i]);
      return out.join(', ');
    }

    async function loadSchedules() {
      const res = await fetch('/api/schedules');
      schedulesCache = await res.json();
      const tbody = document.getElementById('schedulesTableBody');
      tbody.innerHTML = '';

      if (schedulesCache.length === 0) {
        tbody.innerHTML = '<tr><td colspan="7" style="text-align:center;color:var(--text-muted);">Nenhum horário programado.</td></tr>';
      } else {
        schedulesCache.forEach(s => {
          const timeStr = String(s.hour).padStart(2, '0') + ':' + String(s.minute).padStart(2, '0');
          const tr = document.createElement('tr');
          tr.innerHTML = `
            <td><span class="status-pill ${s.enabled ? 'ok' : 'warn'}">${s.enabled ? 'Ativo' : 'Pausado'}</span></td>
            <td><strong>${timeStr}</strong></td>
            <td>${s.description}</td>
            <td>${visualTagHtml(s.visualType || 1)}</td>
            <td>${formatDaysMask(s.daysMask)}</td>
            <td>Toque ${s.track} (${s.durationSec}s)</td>
            <td style="display:flex;gap:6px;flex-wrap:wrap;">
              <button class="btn btn-outline btn-sm" onclick="toggleScheduleEnable(${s.id})">${s.enabled ? 'Pausar' : 'Ativar'}</button>
              <button class="btn btn-outline btn-sm" onclick="editSchedule(${s.id})">Editar</button>
              <button class="btn btn-danger btn-sm" onclick="deleteSchedule(${s.id})">Apagar</button>
            </td>
          `;
          tbody.appendChild(tr);
        });
      }
      renderWeeklyCalendar();
    }

    function renderWeeklyCalendar() {
      const grid = document.getElementById('calendarGrid');
      grid.innerHTML = '';
      for (let d = 1; d <= 7; d++) {
        const dayIdx = d % 7;
        const bit = (1 << dayIdx);
        const col = document.createElement('div');
        col.className = 'day-col';
        col.innerHTML = `<div class="day-col-header">${FULL_DAYS[dayIdx]}</div>`;

        const dayEvents = schedulesCache.filter(s => (s.daysMask & bit));
        if (dayEvents.length === 0) {
          col.innerHTML += `<div style="font-size:12px;color:var(--text-muted);text-align:center;margin-top:20px;">Livre</div>`;
        } else {
          dayEvents.forEach(ev => {
            const t = String(ev.hour).padStart(2, '0') + ':' + String(ev.minute).padStart(2, '0');
            const vClass = visualCalClass(ev.visualType || 1);
            col.innerHTML += `
              <div class="cal-event ${vClass} ${ev.enabled ? '' : 'inactive'}">
                <strong>${t}</strong> · ${ev.durationSec}s (Toque ${ev.track})<br>
                <span>${ev.description}</span>
              </div>
            `;
          });
        }
        grid.appendChild(col);
      }
    }

    function resetScheduleForm() {
      document.getElementById('schedId').value = '0';
      document.getElementById('formSchedTitle').textContent = 'Programar Novo Horário Escolar';
      document.getElementById('schedHour').value = 7;
      document.getElementById('schedMin').value = 30;
      document.getElementById('schedDesc').value = '';
      document.getElementById('schedVisual').value = 1;
      document.getElementById('schedTrack').value = 1;
      document.getElementById('schedDur').value = TRACK_DURATIONS[1];
      document.querySelectorAll('#daysSelector .day-chip').forEach(c => {
        const b = parseInt(c.dataset.bit);
        c.classList.toggle('selected', b >= 1 && b <= 5);
      });
    }

    function editSchedule(id) {
      const s = schedulesCache.find(x => x.id === id);
      if (!s) return;
      document.getElementById('schedId').value = s.id;
      document.getElementById('formSchedTitle').textContent = 'Alterar Horário Escolar';
      document.getElementById('schedHour').value = s.hour;
      document.getElementById('schedMin').value = s.minute;
      document.getElementById('schedDesc').value = s.description;
      document.getElementById('schedVisual').value = s.visualType || 1;
      document.getElementById('schedTrack').value = s.track || 1;
      document.getElementById('schedDur').value = s.durationSec;
      document.querySelectorAll('#daysSelector .day-chip').forEach(c => {
        const b = parseInt(c.dataset.bit);
        c.classList.toggle('selected', (s.daysMask & (1 << b)) !== 0);
      });
      window.scrollTo({ top: 0, behavior: 'smooth' });
    }

    async function saveSchedule() {
      let mask = 0;
      document.querySelectorAll('#daysSelector .day-chip.selected').forEach(c => {
        mask |= (1 << parseInt(c.dataset.bit));
      });
      const trk = parseInt(document.getElementById('schedTrack').value) || 1;
      const payload = {
        id: parseInt(document.getElementById('schedId').value),
        hour: parseInt(document.getElementById('schedHour').value),
        minute: parseInt(document.getElementById('schedMin').value),
        description: document.getElementById('schedDesc').value.trim(),
        visualType: parseInt(document.getElementById('schedVisual').value) || 1,
        track: trk,
        durationSec: parseInt(document.getElementById('schedDur').value) || TRACK_DURATIONS[trk],
        daysMask: mask,
        enabled: true
      };
      const res = await fetch('/api/schedules', { method: 'POST', body: JSON.stringify(payload) });
      const data = await res.json();
      showToast(data.ok ? 'Horário Guardado' : 'Atenção', data.msg, data.ok ? 'ok' : 'err');
      if (data.ok) { resetScheduleForm(); loadSchedules(); pollStatus(); }
    }

    async function toggleScheduleEnable(id) {
      const s = schedulesCache.find(x => x.id === id);
      if (!s) return;
      s.enabled = !s.enabled;
      await fetch('/api/schedules', { method: 'POST', body: JSON.stringify(s) });
      loadSchedules();
      pollStatus();
    }

    async function deleteSchedule(id) {
      if (!confirm('Deseja realmente apagar este horário da lista?')) return;
      const res = await fetch('/api/schedules/delete?id=' + id, { method: 'POST' });
      const data = await res.json();
      showToast('Horário Apagado', data.msg, 'ok');
      loadSchedules();
      pollStatus();
    }

    async function manualRingConfirm() {
      const trk = parseInt(document.getElementById('manTrack').value) || 1;
      const payload = {
        durationSec: parseInt(document.getElementById('manDuration').value) || TRACK_DURATIONS[trk],
        track: trk,
        visualType: parseInt(document.getElementById('manVisual').value) || 3,
        description: document.getElementById('manReason').value
      };
      const res = await fetch('/api/bell/ring', { method: 'POST', body: JSON.stringify(payload) });
      const d = await res.json();
      showToast('Sinal Acionado', d.msg, 'ok');
      pollStatus();
    }

    async function quickVisualRing(vType, trk, descText) {
      const dur = TRACK_DURATIONS[trk] || 8;
      const res = await fetch('/api/bell/ring', {
        method: 'POST',
        body: JSON.stringify({ durationSec: dur, track: trk, visualType: vType, description: descText })
      });
      const d = await res.json();
      showToast('Sinalização em Curso', d.msg, 'ok');
      pollStatus();
    }

    async function manualStopBell() {
      const res = await fetch('/api/bell/stop', { method: 'POST' });
      const d = await res.json();
      showToast('Toque Interrompido', d.msg, 'warn');
      pollStatus();
    }

    async function loadHistory() {
      const res = await fetch('/api/history');
      const list = await res.json();
      const tbody = document.getElementById('historyTableBody');
      tbody.innerHTML = '';
      if (list.length === 0) {
        tbody.innerHTML = '<tr><td colspan="6" style="text-align:center;">Ainda não existem toques registados.</td></tr>';
        return;
      }
      list.forEach(item => {
        const tr = document.createElement('tr');
        tr.innerHTML = `
          <td>${item.dateStr}</td>
          <td>${item.scheduledTime}</td>
          <td><strong>${item.actualTime}</strong></td>
          <td>${item.description}</td>
          <td>${item.origin}</td>
          <td>${historyTagHtml(item.statusMsg, item.success)}</td>
        `;
        tbody.appendChild(tr);
      });
    }

    async function syncBrowserTime() {
      const now = new Date();
      const payload = {
        mode: 'browser',
        year: now.getFullYear(),
        month: now.getMonth() + 1,
        day: now.getDate(),
        hour: now.getHours(),
        minute: now.getMinutes(),
        second: now.getSeconds()
      };
      const res = await fetch('/api/time', { method: 'POST', body: JSON.stringify(payload) });
      const d = await res.json();
      showToast('Relógio Acertado', d.msg, d.ok ? 'ok' : 'err');
      pollStatus();
    }

    async function setManualClock() {
      const dateVal = document.getElementById('cfgDate').value;
      const timeVal = document.getElementById('cfgTime').value;
      if (!dateVal || !timeVal) {
        showToast('Campos em Falta', 'Escolha a data e a hora antes de guardar.', 'warn');
        return;
      }
      const [y, mo, da] = dateVal.split('-').map(Number);
      const [h, mi, s] = (timeVal + ':00').split(':').map(Number);
      const res = await fetch('/api/time', {
        method: 'POST',
        body: JSON.stringify({ mode: 'manual', year: y, month: mo, day: da, hour: h, minute: mi, second: s || 0 })
      });
      const d = await res.json();
      showToast('Hora Guardada', d.msg, d.ok ? 'ok' : 'err');
      pollStatus();
    }

    async function loadConfig() {
      const res = await fetch('/api/config');
      const c = await res.json();
      document.getElementById('cfgSchool').value = c.schoolName || '';
      document.getElementById('cfgVolume').value = c.volume;
      document.getElementById('cfgBtnVisual').value = c.buttonVisual || 3;
      document.getElementById('cfgApSsid').value = c.apSsid || '';
    }

    async function saveConfig() {
      const payload = {
        schoolName: document.getElementById('cfgSchool').value.trim(),
        volume: parseInt(document.getElementById('cfgVolume').value),
        buttonVisual: parseInt(document.getElementById('cfgBtnVisual').value) || 3,
        apSsid: document.getElementById('cfgApSsid').value.trim(),
        apPass: document.getElementById('cfgApPass').value
      };
      const res = await fetch('/api/config', { method: 'POST', body: JSON.stringify(payload) });
      const d = await res.json();
      showToast('Preferências Guardadas', d.msg, d.ok ? 'ok' : 'err');
      pollStatus();
    }

    async function rebootDevice() {
      if (!confirm('Deseja reiniciar a central agora?')) return;
      await fetch('/api/reboot', { method: 'POST' });
      showToast('A Reiniciar', 'O equipamento estará disponível dentro de segundos...', 'warn');
    }

    pollStatus();
    loadSchedules();
    setInterval(pollStatus, 1000);
  </script>
</body>
</html>
)rawliteral";

// ============================================================================
// 4. ESTRUTURAS E VARIÁVEIS GLOBAIS
// ============================================================================
RTC_DS3231 rtc;
WebServer server(80);
Preferences prefs;

struct ScheduleItem {
  uint16_t id;
  bool enabled;
  uint8_t hour;
  uint8_t minute;
  uint8_t daysMask;
  uint16_t durationSec;
  uint8_t track;      // 1=8s, 2=16s, 3=8s, 4=3s
  uint8_t visualType; // 1=Vermelho, 2=Verde, 3=Laranja, 4=Azul
  char description[48];
};

struct HistoryItem {
  char dateStr[12];
  char scheduledTime[8];
  char actualTime[10];
  char description[48];
  char origin[16];
  bool success;
  char statusMsg[40];
};

struct SystemConfig {
  char schoolName[48];
  char apSsid[32];
  char apPass[64];
  uint8_t volume;
  uint16_t defaultDuration;
  uint8_t defaultTrack;
  uint8_t buttonVisual;
} sysConfig;

#define MAX_SCHEDULES 40
#define MAX_HISTORY   35

ScheduleItem schedules[MAX_SCHEDULES];
size_t scheduleCount = 0;

HistoryItem historyList[MAX_HISTORY];
size_t historyCount = 0;

bool dfPlayerOnline = true;
bool bellActive = false;
unsigned long bellStartTime = 0;
unsigned long bellDurationMs = 0;
String bellOrigin = "Inativo";
String bellCurrentDesc = "";
uint32_t lastExecutedMinuteStamp = 0;

// ============================================================================
// 5. ENVIO DIRETO E SEGURO PARA O DFPLAYER
// ============================================================================
void sendRawDfCommand(uint8_t cmd, uint8_t p1, uint8_t p2) {
  uint16_t sum = 0xFF + 0x06 + cmd + 0x00 + p1 + p2;
  uint16_t checksum = 0xFFFF - sum + 1;
  uint8_t frame[10] = {
    0x7E, 0xFF, 0x06, cmd, 0x00, p1, p2,
    (uint8_t)(checksum >> 8), (uint8_t)(checksum & 0xFF), 0xEF
  };
  mySoftwareSerial.write(frame, 10);
}

// ============================================================================
// 6. MOTOR DOS 5 LEDS (SEM PISCAR LARANJA NO FIM DOS TOQUES)
// ============================================================================
enum LedPattern {
  PAT_STARTUP,
  PAT_READY,
  PAT_CLASS_RED,
  PAT_BREAK_GREEN,
  PAT_ALERT_ORANGE,
  PAT_EMERGENCY_BLUE,
  PAT_SUCCESS,
  PAT_RTC_WARNING,
  PAT_RECOVERY,
  PAT_CRITICAL_ERROR
};

class LedManager {
private:
  LedPattern activePattern = PAT_STARTUP;
  LedPattern transientPattern = PAT_READY;
  bool transientActive = false;
  unsigned long transientStartMs = 0;
  unsigned long transientEndMs = 0;
  unsigned long lastStepMs = 0;
  uint16_t stepIndex = 0;
  bool ledStates[5] = {false, false, false, false, false};
  String lastTriggerReason = "Central Pronta";

  void applyHardware() {
    for (int i = 0; i < numLeds; i++) {
      digitalWrite(leds[i], ledStates[i] ? HIGH : LOW);
    }
  }

  void setAll(bool r1, bool r2, bool grn, bool blu, bool org) {
    ledStates[0] = r1;
    ledStates[1] = r2;
    ledStates[2] = grn;
    ledStates[3] = blu;
    ledStates[4] = org;
    applyHardware();
  }

public:
  void initPinsOnly() {
    for (int i = 0; i < numLeds; i++) {
      pinMode(leds[i], OUTPUT);
      digitalWrite(leds[i], LOW);
    }
  }

  void triggerTransient(LedPattern pat, unsigned long durationMs, const String& reason) {
    transientPattern = pat;
    transientActive = true;
    transientStartMs = millis();
    transientEndMs = transientStartMs + durationMs;
    stepIndex = 0;
    lastStepMs = 0;
    lastTriggerReason = reason;
  }

  void showStartup() { triggerTransient(PAT_STARTUP, 2500, "Arranque do Equipamento"); }
  void showReady()   { activePattern = PAT_READY; }

  void showSchoolEvent(uint8_t visualType, unsigned long durationMs, const String& desc) {
    if (visualType == VISUAL_BREAK_GREEN) {
      triggerTransient(PAT_BREAK_GREEN, durationMs, "Intervalo: " + desc);
    } else if (visualType == VISUAL_ALERT_ORANGE) {
      triggerTransient(PAT_ALERT_ORANGE, durationMs, "Atenção: " + desc);
    } else if (visualType == VISUAL_EMERGENCY_BLUE) {
      triggerTransient(PAT_EMERGENCY_BLUE, durationMs, "Emergência: " + desc);
    } else {
      triggerTransient(PAT_CLASS_RED, durationMs, "Aula / Sala: " + desc);
    }
  }

  void showSuccess(const String& msg)  { triggerTransient(PAT_SUCCESS, 1200, msg); }
  void showRecovery(const String& msg) { triggerTransient(PAT_RECOVERY, 1800, msg); }

  void stopEventPattern() {
    transientActive = false;
    stepIndex = 0;
    setAll(false, false, false, false, false);
    lastTriggerReason = "Em Silêncio";
  }

  LedPattern getCurrentPattern() const {
    return transientActive ? transientPattern : activePattern;
  }

  String getPatternName() const {
    switch (getCurrentPattern()) {
      case PAT_STARTUP:        return "Arranque do Sistema";
      case PAT_READY:          return "Em Espera";
      case PAT_CLASS_RED:      return "Vermelho (Aula / Sala)";
      case PAT_BREAK_GREEN:    return "Verde (Intervalo / Recreio)";
      case PAT_ALERT_ORANGE:   return "Laranja (Atenção / Aviso)";
      case PAT_EMERGENCY_BLUE: return "Azul (Emergência / Evacuar)";
      case PAT_SUCCESS:        return "Alteração Guardada";
      case PAT_RTC_WARNING:    return "Relógio em Modo de Reserva";
      case PAT_RECOVERY:       return "Relógio Restabelecido";
      case PAT_CRITICAL_ERROR: return "Aguardar Acerto de Hora";
      default:                 return "Pronto";
    }
  }

  String getLastReason() const { return lastTriggerReason; }
  bool getLedState(int idx) const { return (idx >= 0 && idx < 5) ? ledStates[idx] : false; }

  void update(bool criticalErr, bool rtcErr) {
    unsigned long now = millis();

    if (transientActive && now >= transientEndMs) {
      transientActive = false;
      stepIndex = 0;
      setAll(false, false, false, false, false);
      lastTriggerReason = "Em Silêncio";
    }

    if (criticalErr) activePattern = PAT_CRITICAL_ERROR;
    else if (rtcErr) activePattern = PAT_RTC_WARNING;
    else activePattern = PAT_READY;

    LedPattern current = criticalErr ? PAT_CRITICAL_ERROR : (transientActive ? transientPattern : activePattern);

    switch (current) {
      case PAT_STARTUP:
        if (now - lastStepMs >= 180) {
          lastStepMs = now;
          int pos = stepIndex % 7;
          setAll(pos >= 1, pos >= 2, pos >= 3, pos >= 4, pos >= 5);
          stepIndex++;
        }
        break;

      case PAT_READY:
        if (now - lastStepMs >= 100) {
          lastStepMs = now;
          stepIndex = (stepIndex + 1) % 40;
          setAll(false, false, false, false, (stepIndex == 0));
        }
        break;

      case PAT_CLASS_RED:
        if (now - lastStepMs >= 500) {
          lastStepMs = now;
          bool rOn = (stepIndex % 2 == 0);
          setAll(rOn, rOn, false, false, false);
          stepIndex++;
        }
        break;

      case PAT_BREAK_GREEN:
        setAll(false, false, true, false, false);
        break;

      case PAT_ALERT_ORANGE:
        if (now - lastStepMs >= 600) {
          lastStepMs = now;
          bool oOn = (stepIndex % 2 == 0);
          setAll(false, false, false, false, oOn);
          stepIndex++;
        }
        break;

      case PAT_EMERGENCY_BLUE:
        if (now - lastStepMs >= 200) {
          lastStepMs = now;
          bool bOn = (stepIndex % 2 == 0);
          setAll(false, false, false, bOn, false);
          stepIndex++;
        }
        break;

      case PAT_SUCCESS:
        if (now - lastStepMs >= 180) {
          lastStepMs = now;
          bool oOn = (stepIndex == 0 || stepIndex == 2 || stepIndex == 4);
          setAll(false, false, false, false, oOn);
          stepIndex++;
        }
        break;

      case PAT_RTC_WARNING:
        if (now - lastStepMs >= 220) {
          lastStepMs = now;
          int s = stepIndex % 10;
          bool oOn = (s == 0 || s == 2);
          setAll(false, false, false, false, oOn);
          stepIndex++;
        }
        break;

      case PAT_RECOVERY:
        if (now - lastStepMs >= 160) {
          lastStepMs = now;
          bool oOn = (stepIndex % 2 == 0);
          setAll(false, false, false, false, oOn);
          stepIndex++;
        }
        break;

      case PAT_CRITICAL_ERROR:
        if (now - lastStepMs >= 250) {
          lastStepMs = now;
          bool p = (stepIndex % 2 == 0);
          setAll(p, p, false, false, !p);
          stepIndex++;
        }
        break;
    }
  }
} ledManager;

void addHistoryRecord(const char* schedTime, const char* desc, const char* origin, bool success, const char* msg);

// ============================================================================
// 7. GESTOR DE RELÓGIO COM TOLERÂNCIA A FALHAS (ClockManager)
// ============================================================================
class ClockManager {
private:
  bool rtcOnline = false;
  bool rtcLostPowerFlag = false;
  bool timeValid = false;
  unsigned long lastRtcCheckMs = 0;
  uint32_t rtcStateSeq = 1;

  bool probeI2C() {
    Wire.beginTransmission(0x68);
    return (Wire.endTransmission() == 0);
  }

  void setInternalClock(time_t epoch) {
    struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    timeValid = (epoch > 1704067200UL);
  }

public:
  void initialize() {
    setenv("TZ", "WAT-1", 1);
    tzset();
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

    if (probeI2C() && rtc.begin(&Wire)) {
      rtcOnline = true;
      rtcLostPowerFlag = rtc.lostPower();
      DateTime now = rtc.now();
      if (!rtcLostPowerFlag && now.year() >= 2024 && now.year() <= 2099) {
        setInternalClock(now.unixtime() - 3600UL);
        timeValid = true;
      } else {
        setManualDateTime(2026, 1, 1, 7, 0, 0, "Inicial");
      }
    } else {
      rtcOnline = false;
      rtcStateSeq++;
      struct tm t = {0};
      t.tm_year = 2026 - 1900; t.tm_mon = 0; t.tm_mday = 1;
      t.tm_hour = 7; t.tm_min = 0; t.tm_sec = 0;
      setInternalClock(mktime(&t));
    }
  }

  void update() {
    unsigned long nowMs = millis();
    if (nowMs - lastRtcCheckMs >= 5000) {
      lastRtcCheckMs = nowMs;
      bool present = probeI2C();

      if (rtcOnline && !present) {
        rtcOnline = false;
        rtcStateSeq++;
        addHistoryRecord("--:--", "Relógio em Modo de Reserva", "Sistema", false, "Laranja (Contingência)");
      } else if (!rtcOnline && present) {
        if (rtc.begin(&Wire)) {
          DateTime dt = rtc.now();
          if (dt.year() >= 2024 && dt.year() <= 2099) {
            setInternalClock(dt.unixtime() - 3600UL);
            timeValid = true;
          } else if (timeValid) {
            struct tm ti;
            if (getLocalTimeStruct(ti)) {
              rtc.adjust(DateTime(ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday, ti.tm_hour, ti.tm_min, ti.tm_sec));
            }
          }
          rtcOnline = true;
          rtcLostPowerFlag = false;
          rtcStateSeq++;
          ledManager.showRecovery("Relógio Recuperado");
          addHistoryRecord("--:--", "Relógio Principal Restabelecido", "Sistema", true, "Laranja (Sincronizado)");
        }
      } else if (rtcOnline && present) {
        DateTime dt = rtc.now();
        if (dt.year() >= 2024 && dt.year() <= 2099) {
          setInternalClock(dt.unixtime() - 3600UL);
          timeValid = true;
        }
      }
    }
  }

  bool getLocalTimeStruct(struct tm &timeinfo) {
    time_t now;
    time(&now);
    if (now < 1704067200UL) {
      timeValid = false;
      return false;
    }
    localtime_r(&now, &timeinfo);
    timeValid = true;
    return true;
  }

  bool setManualDateTime(int year, int month, int day, int hour, int minute, int second, const char* sourceName) {
    if (year < 2024 || month < 1 || month > 12 || day < 1 || day > 31) return false;
    struct tm t = {0};
    t.tm_year = year - 1900;
    t.tm_mon  = month - 1;
    t.tm_mday = day;
    t.tm_hour = hour;
    t.tm_min  = minute;
    t.tm_sec  = second;
    time_t epoch = mktime(&t);
    setInternalClock(epoch);

    if (probeI2C()) {
      rtc.adjust(DateTime(year, month, day, hour, minute, second));
      rtcOnline = true;
      rtcLostPowerFlag = false;
    }
    if (String(sourceName) != "Inicial") {
      ledManager.showSuccess("Hora Atualizada");
    }
    addHistoryRecord("--:--", "Acerto de Hora", sourceName, true, "Hora atualizada");
    return true;
  }

  bool isRtcOnline() const { return rtcOnline; }
  bool isTimeValid() const { return timeValid; }
  uint32_t getStateSeq() const { return rtcStateSeq; }
  String getSourceDescription() const {
    return rtcOnline ? "Relógio Principal Automático" : "Relógio Interno de Reserva";
  }
} clockManager;

// ============================================================================
// 8. ARMAZENAMENTO DE HORÁRIOS E CONFIGURAÇÕES
// ============================================================================
void loadSystemConfig() {
  prefs.begin("sinotech", true);
  strlcpy(sysConfig.schoolName, prefs.getString("school", "Complexo Escolar SinoTech").c_str(), sizeof(sysConfig.schoolName));
  strlcpy(sysConfig.apSsid,     prefs.getString("ap_ssid", "Campainha-Escolar").c_str(), sizeof(sysConfig.apSsid));
  strlcpy(sysConfig.apPass,     prefs.getString("ap_pass", "12345678").c_str(), sizeof(sysConfig.apPass));
  sysConfig.volume          = prefs.getUChar("vol", 25);
  sysConfig.defaultDuration = prefs.getUShort("dur", 8);
  sysConfig.defaultTrack    = prefs.getUChar("trk", 1);
  sysConfig.buttonVisual    = prefs.getUChar("btn_v", VISUAL_ALERT_ORANGE);
  prefs.end();
}

void saveSystemConfig() {
  prefs.begin("sinotech", false);
  prefs.putString("school", sysConfig.schoolName);
  prefs.putString("ap_ssid", sysConfig.apSsid);
  prefs.putString("ap_pass", sysConfig.apPass);
  prefs.putUChar("vol", sysConfig.volume);
  prefs.putUShort("dur", sysConfig.defaultDuration);
  prefs.putUChar("trk", sysConfig.defaultTrack);
  prefs.putUChar("btn_v", sysConfig.buttonVisual);
  prefs.end();
}

uint8_t inferVisualTypeFromDesc(const char* desc) {
  String s = String(desc);
  s.toLowerCase();
  if (s.indexOf("intervalo") >= 0 || s.indexOf("recreio") >= 0) return VISUAL_BREAK_GREEN;
  if (s.indexOf("emerg") >= 0 || s.indexOf("evacua") >= 0)      return VISUAL_EMERGENCY_BLUE;
  if (s.indexOf("aten") >= 0 || s.indexOf("aviso") >= 0)        return VISUAL_ALERT_ORANGE;
  return VISUAL_CLASS_RED;
}

void sortSchedules() {
  for (size_t i = 0; i < scheduleCount; i++) {
    for (size_t j = i + 1; j < scheduleCount; j++) {
      uint16_t tA = schedules[i].hour * 60 + schedules[i].minute;
      uint16_t tB = schedules[j].hour * 60 + schedules[j].minute;
      if (tB < tA) {
        ScheduleItem tmp = schedules[i];
        schedules[i] = schedules[j];
        schedules[j] = tmp;
      }
    }
  }
}

void saveSchedulesToFS() {
  sortSchedules();
  File f = LittleFS.open("/schedules.json", "w");
  if (!f) return;
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (size_t i = 0; i < scheduleCount; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["id"]          = schedules[i].id;
    o["enabled"]     = schedules[i].enabled;
    o["hour"]        = schedules[i].hour;
    o["minute"]      = schedules[i].minute;
    o["daysMask"]    = schedules[i].daysMask;
    o["durationSec"] = schedules[i].durationSec;
    o["track"]       = schedules[i].track;
    o["visualType"]  = schedules[i].visualType;
    o["description"] = schedules[i].description;
  }
  serializeJson(doc, f);
  f.close();
}

void createDefaultSchedules() {
  const struct { uint8_t h; uint8_t m; const char* d; uint8_t trk; uint8_t v; } defaults[] = {
    { 7, 30, "Entrada dos Alunos (Manhã)",   1, VISUAL_CLASS_RED},
    { 7, 45, "Início das Aulas",             1, VISUAL_CLASS_RED},
    { 9, 55, "Aviso Prévio de Intervalo",    3, VISUAL_ALERT_ORANGE},
    {10,  0, "Intervalo / Recreio",          2, VISUAL_BREAK_GREEN},
    {10, 20, "Voltar para a Sala",           1, VISUAL_CLASS_RED},
    {12,  0, "Fim das Aulas / Troca Turno",  1, VISUAL_CLASS_RED},
    {13,  0, "Início das Aulas (Tarde)",     1, VISUAL_CLASS_RED},
    {15, 30, "Intervalo / Recreio Tarde",    2, VISUAL_BREAK_GREEN},
    {17, 15, "Fim das Aulas (Encerramento)", 1, VISUAL_CLASS_RED}
  };
  scheduleCount = 9;
  for (size_t i = 0; i < scheduleCount; i++) {
    schedules[i].id          = i + 1;
    schedules[i].enabled     = true;
    schedules[i].hour        = defaults[i].h;
    schedules[i].minute      = defaults[i].m;
    schedules[i].daysMask    = 0b00111110;
    schedules[i].track       = defaults[i].trk;
    schedules[i].durationSec = getTrackDurationSec(defaults[i].trk);
    schedules[i].visualType  = defaults[i].v;
    strlcpy(schedules[i].description, defaults[i].d, sizeof(schedules[i].description));
  }
  saveSchedulesToFS();
}

void loadSchedulesFromFS() {
  if (!LittleFS.exists("/schedules.json")) {
    createDefaultSchedules();
    return;
  }
  File f = LittleFS.open("/schedules.json", "r");
  if (!f) return;
  JsonDocument doc;
  bool updatedOldDurations = false;
  if (deserializeJson(doc, f) == DeserializationError::Ok && doc.is<JsonArray>()) {
    scheduleCount = 0;
    for (JsonObject o : doc.as<JsonArray>()) {
      if (scheduleCount >= MAX_SCHEDULES) break;
      schedules[scheduleCount].id          = o["id"] | (scheduleCount + 1);
      schedules[scheduleCount].enabled     = o["enabled"] | true;
      schedules[scheduleCount].hour        = o["hour"] | 8;
      schedules[scheduleCount].minute      = o["minute"] | 0;
      schedules[scheduleCount].daysMask    = o["daysMask"] | 62;
      uint8_t trk = o["track"] | 1;
      schedules[scheduleCount].track       = trk;
      uint16_t savedDur = o["durationSec"] | getTrackDurationSec(trk);
      if (savedDur == 4 || savedDur == 5 || savedDur == 6) {
        savedDur = getTrackDurationSec(trk);
        updatedOldDurations = true;
      }
      schedules[scheduleCount].durationSec = savedDur;
      strlcpy(schedules[scheduleCount].description, o["description"] | "Evento Escolar", sizeof(schedules[scheduleCount].description));
      schedules[scheduleCount].visualType  = o["visualType"] | inferVisualTypeFromDesc(schedules[scheduleCount].description);
      scheduleCount++;
    }
    sortSchedules();
  }
  f.close();
  if (updatedOldDurations) {
    saveSchedulesToFS();
  }
}

void addHistoryRecord(const char* schedTime, const char* desc, const char* origin, bool success, const char* msg) {
  size_t limit = (historyCount < MAX_HISTORY) ? historyCount : (MAX_HISTORY - 1);
  for (size_t i = limit; i > 0; i--) {
    historyList[i] = historyList[i - 1];
  }
  if (historyCount < MAX_HISTORY) historyCount++;

  struct tm ti;
  if (clockManager.getLocalTimeStruct(ti)) {
    snprintf(historyList[0].dateStr, sizeof(historyList[0].dateStr), "%04d-%02d-%02d", ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
    snprintf(historyList[0].actualTime, sizeof(historyList[0].actualTime), "%02d:%02d:%02d", ti.tm_hour, ti.tm_min, ti.tm_sec);
  } else {
    strlcpy(historyList[0].dateStr, "Hoje", sizeof(historyList[0].dateStr));
    snprintf(historyList[0].actualTime, sizeof(historyList[0].actualTime), "+%lus", millis() / 1000);
  }

  strlcpy(historyList[0].scheduledTime, schedTime, sizeof(historyList[0].scheduledTime));
  strlcpy(historyList[0].description, desc, sizeof(historyList[0].description));
  strlcpy(historyList[0].origin, origin, sizeof(historyList[0].origin));
  historyList[0].success = success;
  strlcpy(historyList[0].statusMsg, msg, sizeof(historyList[0].statusMsg));
}

// ============================================================================
// 9. CONTROLO DE TOQUE (SEM TEXTOS TÉCNICOS E SEM PISCAR LARANJA NO FIM)
// ============================================================================
const char* getVisualLabel(uint8_t vType) {
  if (vType == VISUAL_BREAK_GREEN)    return "Verde (Intervalo)";
  if (vType == VISUAL_ALERT_ORANGE)   return "Laranja (Atenção)";
  if (vType == VISUAL_EMERGENCY_BLUE) return "Azul (Emergência)";
  return "Vermelho (Aula / Sala)";
}

bool triggerBell(uint16_t durationSec, uint8_t track, uint8_t visualType, const char* origin, const char* desc, const char* schedTimeStr) {
  if (durationSec < 1) durationSec = getTrackDurationSec(track);
  if (durationSec > 60) durationSec = 60;

  bellActive = true;
  bellStartTime = millis();
  bellDurationMs = (unsigned long)durationSec * 1000UL;
  bellOrigin = origin;
  bellCurrentDesc = desc;

  myDFPlayer.playMp3Folder(track);
  ledManager.showSchoolEvent(visualType, bellDurationMs, desc);

  addHistoryRecord(schedTimeStr, desc, origin, true, getVisualLabel(visualType));
  return true;
}

void tocarCampainha() {
  uint8_t vType = sysConfig.buttonVisual;
  uint8_t btnTrack = vType; // Usa o som correspondente à cor configurada para o botão físico
  triggerBell(getTrackDurationSec(btnTrack), btnTrack, vType, "Botão Local", "Toque Manual no Botão", "Manual");
}

void finishBellNaturally() {
  bellActive = false;
  bellOrigin = "Inativo";
  bellCurrentDesc = "";
  ledManager.stopEventPattern();
}

void forceStopBellSafely() {
  if (!bellActive) return;
  bellActive = false;
  bellOrigin = "Inativo";
  bellCurrentDesc = "";
  sendRawDfCommand(0x16, 0x00, 0x00);
  ledManager.stopEventPattern();
}

// ============================================================================
// 10. AGENDADOR AUTOMÁTICO E BOTÃO FÍSICO
// ============================================================================
void updateAutomationEngine() {
  unsigned long nowMs = millis();

  if (bellActive && (nowMs - bellStartTime >= bellDurationMs)) {
    finishBellNaturally();
  }

  static int lastBtnRaw = HIGH;
  static int stableBtnState = HIGH;
  static unsigned long lastDebounceTime = 0;

  int reading = digitalRead(pinoBotao);
  if (reading != lastBtnRaw) {
    lastDebounceTime = nowMs;
  }
  if ((nowMs - lastDebounceTime) > 50) {
    if (reading != stableBtnState) {
      stableBtnState = reading;
      if (stableBtnState == LOW) {
        if (!bellActive) {
          tocarCampainha();
        } else {
          forceStopBellSafely();
        }
      }
    }
  }
  lastBtnRaw = reading;

  static unsigned long lastSchedCheck = 0;
  if (nowMs - lastSchedCheck < 500) return;
  lastSchedCheck = nowMs;

  if (!clockManager.isTimeValid()) return;
  struct tm ti;
  if (!clockManager.getLocalTimeStruct(ti)) return;

  uint32_t currentMinuteStamp = ((uint32_t)ti.tm_year * 530000UL) + ((uint32_t)ti.tm_yday * 1440UL) + ((uint32_t)ti.tm_hour * 60UL) + (uint32_t)ti.tm_min;
  if (currentMinuteStamp == lastExecutedMinuteStamp) return;

  uint8_t todayBit = (1 << ti.tm_wday);

  for (size_t i = 0; i < scheduleCount; i++) {
    if (!schedules[i].enabled) continue;
    if ((schedules[i].daysMask & todayBit) == 0) continue;

    if (schedules[i].hour == ti.tm_hour && schedules[i].minute == ti.tm_min && ti.tm_sec <= 10) {
      lastExecutedMinuteStamp = currentMinuteStamp;
      char schedBuf[8];
      snprintf(schedBuf, sizeof(schedBuf), "%02d:%02d", schedules[i].hour, schedules[i].minute);
      triggerBell(schedules[i].durationSec, schedules[i].track, schedules[i].visualType, "Automático", schedules[i].description, schedBuf);
      break;
    }
  }
}

// ============================================================================
// 11. ENDPOINTS DO SERVIDOR WEB
// ============================================================================
void sendJsonResponse(int code, const JsonDocument& doc) {
  String out;
  serializeJson(doc, out);
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(code, "application/json; charset=utf-8", out);
}

void handleApiStatus() {
  JsonDocument doc;
  struct tm ti;
  bool validTime = clockManager.getLocalTimeStruct(ti);

  if (validTime) {
    char timeBuf[12], dateBuf[16];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", ti.tm_hour, ti.tm_min, ti.tm_sec);
    snprintf(dateBuf, sizeof(dateBuf), "%04d-%02d-%02d", ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
    doc["time"]    = timeBuf;
    doc["date"]    = dateBuf;
    doc["weekday"] = ti.tm_wday;
  } else {
    doc["time"]    = "--:--:--";
    doc["date"]    = "A aguardar acerto";
    doc["weekday"] = -1;
  }

  doc["timeValid"]        = validTime;
  doc["rtcOnline"]        = clockManager.isRtcOnline();
  doc["timeSource"]       = clockManager.getSourceDescription();
  doc["rtcStateSeq"]      = clockManager.getStateSeq();
  doc["dfPlayerOnline"]   = dfPlayerOnline;
  doc["bellActive"]       = bellActive;
  doc["bellRemainingSec"] = bellActive ? (int)((bellDurationMs - (millis() - bellStartTime)) / 1000) : 0;
  doc["schoolName"]       = sysConfig.schoolName;
  doc["defaultDuration"]  = sysConfig.defaultDuration;

  JsonObject ledsObj = doc["leds"].to<JsonObject>();
  ledsObj["pattern"] = ledManager.getPatternName();
  ledsObj["reason"]  = ledManager.getLastReason();
  JsonArray statesArr = ledsObj["states"].to<JsonArray>();
  for (int i = 0; i < numLeds; i++) {
    statesArr.add(ledManager.getLedState(i));
  }

  int todayCount = 0;
  String nextTimeStr = "Concluído";
  String nextDescStr = "Sem mais toques hoje";

  if (validTime) {
    uint8_t todayBit = (1 << ti.tm_wday);
    int nowMins = ti.tm_hour * 60 + ti.tm_min;
    bool foundToday = false;

    for (size_t i = 0; i < scheduleCount; i++) {
      if (!schedules[i].enabled) continue;
      if (schedules[i].daysMask & todayBit) {
        todayCount++;
        int sMins = schedules[i].hour * 60 + schedules[i].minute;
        if (!foundToday && sMins > nowMins) {
          char b[8];
          snprintf(b, sizeof(b), "%02d:%02d", schedules[i].hour, schedules[i].minute);
          nextTimeStr = b;
          nextDescStr = schedules[i].description;
          foundToday = true;
        }
      }
    }
    if (!foundToday && scheduleCount > 0) {
      const char* dNames[] = {"Dom", "Seg", "Ter", "Qua", "Qui", "Sex", "Sáb"};
      for (int dOffset = 1; dOffset <= 7 && !foundToday; dOffset++) {
        int checkDay = (ti.tm_wday + dOffset) % 7;
        uint8_t dayBit = (1 << checkDay);
        for (size_t i = 0; i < scheduleCount; i++) {
          if (schedules[i].enabled && (schedules[i].daysMask & dayBit)) {
            char b[16];
            snprintf(b, sizeof(b), "%s %02d:%02d", dNames[checkDay], schedules[i].hour, schedules[i].minute);
            nextTimeStr = b;
            nextDescStr = schedules[i].description;
            foundToday = true;
            break;
          }
        }
      }
    }
  }

  int activeTotal = 0;
  for (size_t i = 0; i < scheduleCount; i++) if (schedules[i].enabled) activeTotal++;

  doc["todayEventsCount"]     = todayCount;
  doc["totalActiveSchedules"] = activeTotal;
  doc["nextEventTime"]        = nextTimeStr;
  doc["nextEventDesc"]        = nextDescStr;

  if (historyCount > 0) {
    doc["lastEventTime"] = historyList[0].actualTime;
    doc["lastEventDesc"] = historyList[0].description;
  } else {
    doc["lastEventTime"] = "--:--";
    doc["lastEventDesc"] = "Nenhum";
  }

  sendJsonResponse(200, doc);
}

void handlePostTime() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok) {
    server.send(400, "application/json", "{\"ok\":false,\"msg\":\"Pedido inválido\"}");
    return;
  }

  int y  = doc["year"] | 0;
  int mo = doc["month"] | 0;
  int d  = doc["day"] | 0;
  int h  = doc["hour"] | 0;
  int mi = doc["minute"] | 0;
  int s  = doc["second"] | 0;

  bool ok = clockManager.setManualDateTime(y, mo, d, h, mi, s, "Painel");
  JsonDocument res;
  res["ok"]  = ok;
  res["msg"] = ok ? "A hora da escola foi acertada com sucesso!" : "Data ou hora inválida.";
  sendJsonResponse(ok ? 200 : 400, res);
}

void handleGetSchedules() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (size_t i = 0; i < scheduleCount; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["id"]          = schedules[i].id;
    o["enabled"]     = schedules[i].enabled;
    o["hour"]        = schedules[i].hour;
    o["minute"]      = schedules[i].minute;
    o["daysMask"]    = schedules[i].daysMask;
    o["durationSec"] = schedules[i].durationSec;
    o["track"]       = schedules[i].track;
    o["visualType"]  = schedules[i].visualType;
    o["description"] = schedules[i].description;
  }
  sendJsonResponse(200, doc);
}

void handleSaveSchedule() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok) {
    server.send(400, "application/json", "{\"ok\":false,\"msg\":\"Erro nos dados\"}");
    return;
  }

  int id               = doc["id"] | 0;
  uint8_t hour         = doc["hour"] | 0;
  uint8_t minute       = doc["minute"] | 0;
  uint8_t daysMask     = doc["daysMask"] | 0;
  uint8_t track        = constrain((int)(doc["track"] | 1), 1, 99);
  uint16_t durationSec = doc["durationSec"] | getTrackDurationSec(track);
  uint8_t visualType   = constrain((int)(doc["visualType"] | VISUAL_CLASS_RED), 1, 4);
  bool enabled         = doc["enabled"] | true;
  const char* desc     = doc["description"] | "Toque Escolar";

  if (hour > 23 || minute > 59 || daysMask == 0 || strlen(desc) == 0) {
    server.send(400, "application/json", "{\"ok\":false,\"msg\":\"Indique o nome do toque e escolha pelo menos um dia da semana.\"}");
    return;
  }

  if (id > 0) {
    for (size_t i = 0; i < scheduleCount; i++) {
      if (schedules[i].id == id) {
        schedules[i].hour        = hour;
        schedules[i].minute      = minute;
        schedules[i].daysMask    = daysMask;
        schedules[i].durationSec = constrain(durationSec, 1, 60);
        schedules[i].track       = track;
        schedules[i].visualType  = visualType;
        schedules[i].enabled     = enabled;
        strlcpy(schedules[i].description, desc, sizeof(schedules[i].description));
        saveSchedulesToFS();
        ledManager.showSuccess("Horário Alterado");
        server.send(200, "application/json", "{\"ok\":true,\"msg\":\"Horário atualizado com sucesso.\"}");
        return;
      }
    }
  }

  if (scheduleCount >= MAX_SCHEDULES) {
    server.send(400, "application/json", "{\"ok\":false,\"msg\":\"Lista de horários cheia.\"}");
    return;
  }

  uint16_t nextId = 1;
  for (size_t i = 0; i < scheduleCount; i++) {
    if (schedules[i].id >= nextId) nextId = schedules[i].id + 1;
  }

  schedules[scheduleCount].id          = nextId;
  schedules[scheduleCount].enabled     = enabled;
  schedules[scheduleCount].hour        = hour;
  schedules[scheduleCount].minute      = minute;
  schedules[scheduleCount].daysMask    = daysMask;
  schedules[scheduleCount].durationSec = constrain(durationSec, 1, 60);
  schedules[scheduleCount].track       = track;
  schedules[scheduleCount].visualType  = visualType;
  strlcpy(schedules[scheduleCount].description, desc, sizeof(schedules[scheduleCount].description));
  scheduleCount++;

  saveSchedulesToFS();
  ledManager.showSuccess("Horário Guardado");
  server.send(200, "application/json", "{\"ok\":true,\"msg\":\"Novo horário escolar guardado!\"}");
}

void handleDeleteSchedule() {
  int id = server.arg("id").toInt();
  for (size_t i = 0; i < scheduleCount; i++) {
    if (schedules[i].id == id) {
      for (size_t j = i; j < scheduleCount - 1; j++) {
        schedules[j] = schedules[j + 1];
      }
      scheduleCount--;
      saveSchedulesToFS();
      ledManager.showSuccess("Horário Apagado");
      server.send(200, "application/json", "{\"ok\":true,\"msg\":\"Horário removido da agenda.\"}");
      return;
    }
  }
  server.send(404, "application/json", "{\"ok\":false,\"msg\":\"Horário não encontrado.\"}");
}

void handleBellRing() {
  JsonDocument doc;
  deserializeJson(doc, server.arg("plain"));
  uint8_t track      = constrain((int)(doc["track"] | 1), 1, 99);
  uint16_t dur       = doc["durationSec"] | getTrackDurationSec(track);
  uint8_t visualType = constrain((int)(doc["visualType"] | VISUAL_ALERT_ORANGE), 1, 4);
  const char* desc   = doc["description"] | "Toque Manual";

  triggerBell(dur, track, visualType, "Manual", desc, "Manual");
  JsonDocument res;
  res["ok"]  = true;
  res["msg"] = String("Sinal acionado: ") + getVisualLabel(visualType);
  sendJsonResponse(200, res);
}

void handleBellStop() {
  forceStopBellSafely();
  addHistoryRecord("Manual", "Toque Interrompido", "Direção", true, "Parado manualmente");
  server.send(200, "application/json", "{\"ok\":true,\"msg\":\"O toque foi interrompido com segurança.\"}");
}

void handleGetHistory() {
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (size_t i = 0; i < historyCount; i++) {
    JsonObject o = arr.add<JsonObject>();
    o["dateStr"]       = historyList[i].dateStr;
    o["scheduledTime"] = historyList[i].scheduledTime;
    o["actualTime"]    = historyList[i].actualTime;
    o["description"]   = historyList[i].description;
    o["origin"]        = historyList[i].origin;
    o["success"]       = historyList[i].success;
    o["statusMsg"]     = historyList[i].statusMsg;
  }
  sendJsonResponse(200, doc);
}

void handleGetConfig() {
  JsonDocument doc;
  doc["schoolName"]      = sysConfig.schoolName;
  doc["apSsid"]          = sysConfig.apSsid;
  doc["volume"]          = sysConfig.volume;
  doc["defaultDuration"] = sysConfig.defaultDuration;
  doc["buttonVisual"]    = sysConfig.buttonVisual;
  sendJsonResponse(200, doc);
}

void handlePostConfig() {
  JsonDocument doc;
  if (deserializeJson(doc, server.arg("plain")) != DeserializationError::Ok) {
    server.send(400, "application/json", "{\"ok\":false,\"msg\":\"Dados inválidos\"}");
    return;
  }

  if (doc["schoolName"].is<const char*>()) strlcpy(sysConfig.schoolName, doc["schoolName"], sizeof(sysConfig.schoolName));
  if (doc["apSsid"].is<const char*>() && strlen(doc["apSsid"]) >= 4) {
    strlcpy(sysConfig.apSsid, doc["apSsid"], sizeof(sysConfig.apSsid));
  }
  if (doc["apPass"].is<const char*>() && strlen(doc["apPass"]) >= 8) {
    strlcpy(sysConfig.apPass, doc["apPass"], sizeof(sysConfig.apPass));
  }

  sysConfig.volume       = constrain((int)(doc["volume"] | sysConfig.volume), 0, 30);
  sysConfig.buttonVisual = constrain((int)(doc["buttonVisual"] | sysConfig.buttonVisual), 1, 4);

  saveSystemConfig();
  sendRawDfCommand(0x06, 0x00, sysConfig.volume);

  ledManager.showSuccess("Preferências Guardadas");
  server.send(200, "application/json", "{\"ok\":true,\"msg\":\"Preferências guardadas com sucesso!\"}");
}

// ============================================================================
// 12. SETUP E LOOP PRINCIPAL
// ============================================================================
void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  Serial.begin(115200);
  pinMode(pinoBotao, INPUT_PULLUP);

  ledManager.initPinsOnly();
  delay(400);

  LittleFS.begin(true);
  loadSystemConfig();
  loadSchedulesFromFS();

  WiFi.persistent(false);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  WiFi.softAP(sysConfig.apSsid, sysConfig.apPass, 6, 0, 4);
  delay(150);
  WiFi.setTxPower(WIFI_POWER_11dBm);
  delay(250);

  startNativeCaptiveDns();

  mySoftwareSerial.begin(9600, SERIAL_8N1, 16, 17);
  if (myDFPlayer.begin(mySoftwareSerial)) {
    dfPlayerOnline = true;
  } else {
    dfPlayerOnline = true;
  }
  delay(100);
  myDFPlayer.volume(sysConfig.volume); // Volume 25
  delay(100);

  clockManager.initialize();

  server.on("/", HTTP_GET, []() {
    server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
  });
  server.on("/generate_204", HTTP_GET, []() {
    server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
  });
  server.on("/gen_204", HTTP_GET, []() {
    server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
  });
  server.on("/hotspot-detect.html", HTTP_GET, []() {
    server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
  });
  server.on("/connecttest.txt", HTTP_GET, []() {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
  });
  server.on("/ncsi.txt", HTTP_GET, []() {
    server.sendHeader("Location", "http://192.168.4.1/", true);
    server.send(302, "text/plain", "");
  });

  server.on("/api/status",           HTTP_GET,  handleApiStatus);
  server.on("/api/time",             HTTP_POST, handlePostTime);
  server.on("/api/schedules",        HTTP_GET,  handleGetSchedules);
  server.on("/api/schedules",        HTTP_POST, handleSaveSchedule);
  server.on("/api/schedules/delete", HTTP_POST, handleDeleteSchedule);
  server.on("/api/bell/ring",        HTTP_POST, handleBellRing);
  server.on("/api/bell/stop",        HTTP_POST, handleBellStop);
  server.on("/api/history",          HTTP_GET,  handleGetHistory);
  server.on("/api/config",           HTTP_GET,  handleGetConfig);
  server.on("/api/config",           HTTP_POST, handlePostConfig);
  server.on("/api/reboot",           HTTP_POST, []() {
    server.send(200, "application/json", "{\"ok\":true}");
    delay(400);
    ESP.restart();
  });
  server.onNotFound([]() {
    server.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
  });

  server.begin();

  ledManager.showStartup();
  addHistoryRecord("--:--", "Central Ligada", "Sistema", true, "Pronto");
}

void loop() {
  processNativeCaptiveDns();
  server.handleClient();
  clockManager.update();
  updateAutomationEngine();

  bool criticalErr = !clockManager.isTimeValid();
  bool rtcErr      = !clockManager.isRtcOnline();
  ledManager.update(criticalErr, rtcErr);
}