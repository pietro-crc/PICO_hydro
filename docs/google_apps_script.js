const SHEET_NAME = 'PICO Hydro Logs';
const EXPECTED_TOKEN = 'replace-with-the-same-long-random-token';

const HEADERS = [
  'received_at',
  'device_uptime_s',
  'firmware',
  'air_temp_c',
  'humidity_percent',
  'water_1_c',
  'water_2_c',
  'water_3_c',
  'water_4_c',
  'tds_ppm',
  'tds_voltage',
  'tds_raw',
  'light_lux',
  'light_raw',
  'water_level',
  'flow_l_min',
  'total_liters',
  'dht_failures',
  'veml_failures',
  'water_temp_failures',
  'wifi_failures',
  'device_status',
];

function doPost(e) {
  try {
    const payload = JSON.parse(e.postData.contents || '{}');

    if (payload.token !== EXPECTED_TOKEN) {
      return jsonResponse({ ok: false, error: 'invalid_token' });
    }

    const sheet = getLogSheet();
    ensureHeader(sheet);
    sheet.appendRow([
      new Date(),
      payload.device_uptime_s ?? '',
      payload.firmware ?? '',
      payload.air_temp_c ?? '',
      payload.humidity_percent ?? '',
      payload.water_1_c ?? '',
      payload.water_2_c ?? '',
      payload.water_3_c ?? '',
      payload.water_4_c ?? '',
      payload.tds_ppm ?? '',
      payload.tds_voltage ?? '',
      payload.tds_raw ?? '',
      payload.light_lux ?? '',
      payload.light_raw ?? '',
      payload.water_level ?? '',
      payload.flow_l_min ?? '',
      payload.total_liters ?? '',
      payload.dht_failures ?? '',
      payload.veml_failures ?? '',
      payload.water_temp_failures ?? '',
      payload.wifi_failures ?? '',
      payload.device_status ?? '',
    ]);

    return jsonResponse({ ok: true });
  } catch (error) {
    return jsonResponse({ ok: false, error: String(error) });
  }
}

function getLogSheet() {
  const spreadsheet = SpreadsheetApp.getActiveSpreadsheet();
  return spreadsheet.getSheetByName(SHEET_NAME) || spreadsheet.insertSheet(SHEET_NAME);
}

function ensureHeader(sheet) {
  if (sheet.getLastRow() === 0) {
    sheet.appendRow(HEADERS);
  }
}

function jsonResponse(value) {
  return ContentService
    .createTextOutput(JSON.stringify(value))
    .setMimeType(ContentService.MimeType.JSON);
}
