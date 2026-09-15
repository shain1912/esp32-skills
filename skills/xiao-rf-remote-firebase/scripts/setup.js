#!/usr/bin/env node
/*
 * RF 리모컨 Firebase 프로비저닝 (프로젝트 생성 + 웹 앱 등록 이후에 실행)
 *
 *   1. 웹 앱 설정에서 apiKey 조회
 *   2. 기본 Realtime Database 생성 (없을 때만)
 *      firebase CLI 의 database:instances:create 는 "첫 기본 DB" 생성을 거부하므로 CLI 내부 API 를 쓴다.
 *   3. 템플릿 -> <out>/firebase, <out>/RemoteFirebase 생성 (secrets.h, 보안 규칙 포함)
 *   4. firebase deploy --only hosting,auth,database   (--skip-deploy 로 생략 가능)
 *   5. ESP32 기기 계정 생성. 이미 있으면 비밀번호를 secrets.h 값으로 맞춘다.
 *      Email/Password 로그인은 4번 auth 배포로 켜지므로 반드시 배포 뒤에 한다.
 *
 * 사용:
 *   node setup.js --project <id> --owner a@gmail.com[,b@gmail.com] --ssid <wifi> --wifi-pass <pw> --out <dir>
 *                 [--region asia-southeast1|us-central1|europe-west1] [--device-email xiao@<id>.local] [--skip-deploy]
 *
 * 다시 실행해도 안전하다. <out>/RemoteFirebase/secrets.h 가 있으면 기기 비밀번호를 재사용한다.
 */
const { execSync } = require("child_process");
const crypto = require("crypto");
const fs = require("fs");
const path = require("path");

const fail = (msg) => { console.error("✖ " + msg); process.exit(1); };
const ok = (msg) => console.log("✔ " + msg);

function arg(name, def) {
  const i = process.argv.indexOf("--" + name);
  if (i >= 0 && i + 1 < process.argv.length) return process.argv[i + 1];
  if (def !== undefined) return def;
  fail(`--${name} 값이 필요합니다`);
}

const PROJECT = arg("project");
const OWNERS = arg("owner").split(",").map((s) => s.trim().toLowerCase()).filter(Boolean);
const WIFI_SSID = arg("ssid");
const WIFI_PASS = arg("wifi-pass");
const OUT = path.resolve(arg("out"));
const REGION = arg("region", "asia-southeast1");
const DEVICE_EMAIL = arg("device-email", `xiao@${PROJECT}.local`).toLowerCase();
const SKIP_DEPLOY = process.argv.includes("--skip-deploy");
const TEMPLATES = path.join(__dirname, "..", "templates");

const EMAIL_RE = /^[a-z0-9._%+-]+@[a-z0-9.-]+\.[a-z]{2,}$/;
for (const e of [...OWNERS, DEVICE_EMAIL]) if (!EMAIL_RE.test(e)) fail(`이메일 형식이 이상합니다: ${e}`);
if (!OWNERS.length) fail("--owner 에 구글 계정을 하나 이상 넣으세요");

/* ---------- firebase-tools 내부 모듈 ---------- */

let lib;
try {
  lib = path.join(execSync("npm root -g", { encoding: "utf8" }).trim(), "firebase-tools", "lib");
  require.resolve(path.join(lib, "auth"));
} catch {
  fail("firebase-tools 를 찾을 수 없습니다. `npm i -g firebase-tools` 후 다시 실행하세요.");
}
const fbAuth = require(path.join(lib, "auth"));
const { requireAuth } = require(path.join(lib, "requireAuth"));
const apiv2 = require(path.join(lib, "apiv2"));
const apps = require(path.join(lib, "management", "apps"));
const database = require(path.join(lib, "management", "database"));

/* ---------- 유틸 ---------- */

async function postJson(url, body, headers = {}) {
  const res = await fetch(url, {
    method: "POST",
    headers: { "Content-Type": "application/json", ...headers },
    body: JSON.stringify(body),
  });
  let json = {};
  try { json = await res.json(); } catch {}
  return { ok: res.ok, status: res.status, body: json };
}

function randomPassword(len) {
  const chars = "abcdefghijkmnpqrstuvwxyzABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  return Array.from(crypto.randomBytes(len), (b) => chars[b % chars.length]).join("");
}

const cString = (s) => s.replace(/\\/g, "\\\\").replace(/"/g, '\\"');

function render(srcDir, dstDir, vars) {
  fs.mkdirSync(dstDir, { recursive: true });
  for (const ent of fs.readdirSync(srcDir, { withFileTypes: true })) {
    const src = path.join(srcDir, ent.name);
    if (ent.isDirectory()) { render(src, path.join(dstDir, ent.name), vars); continue; }
    if (!ent.name.endsWith(".tmpl")) { fs.copyFileSync(src, path.join(dstDir, ent.name)); continue; }
    const text = fs.readFileSync(src, "utf8").replace(/\{\{(\w+)\}\}/g, (m, k) => {
      if (!(k in vars)) fail(`템플릿 변수 없음: ${k} (${src})`);
      return vars[k];
    });
    fs.writeFileSync(path.join(dstDir, ent.name.slice(0, -5)), text);
  }
}

/* ---------- main ---------- */

(async () => {
  const account = fbAuth.getGlobalDefaultAccount();
  if (!account) fail("Firebase CLI 로그인이 안 되어 있습니다. `firebase login` 을 먼저 하세요.");
  await requireAuth({ user: account.user, tokens: account.tokens });
  ok(`Firebase 계정: ${account.user.email}`);

  // 1. 웹 앱 -> apiKey
  const webApps = await apps.listFirebaseApps(PROJECT, apps.AppPlatform.WEB);
  if (!webApps.length) fail(`웹 앱이 없습니다. 먼저: firebase apps:create WEB rf-remote-web --project ${PROJECT}`);
  const cfg = await apps.getAppConfig(webApps[0].appId, apps.AppPlatform.WEB);
  if (!cfg.apiKey) fail("웹 앱 설정에서 apiKey 를 찾지 못했습니다");
  ok(`웹 앱: ${webApps[0].appId}`);

  // 2. 기본 Realtime Database
  const instances = await database.listDatabaseInstances(PROJECT, database.DatabaseLocation.ANY);
  let db = instances.find((i) => String(i.type).toUpperCase() === "DEFAULT_DATABASE");
  if (db) {
    ok(`Realtime Database 이미 있음: ${db.databaseUrl}`);
  } else {
    const loc = database.parseDatabaseLocation(REGION, null);
    if (!loc) fail(`지원하지 않는 리전: ${REGION}`);
    db = await database.createInstance(PROJECT, `${PROJECT}-default-rtdb`, loc, database.DatabaseInstanceType.DEFAULT_DATABASE);
    ok(`Realtime Database 생성: ${db.databaseUrl}`);
  }

  // 3. 파일 생성
  const sketchDir = path.join(OUT, "RemoteFirebase");
  const fbDir = path.join(OUT, "firebase");
  const secretsPath = path.join(sketchDir, "secrets.h");
  let devicePassword = randomPassword(24);
  if (fs.existsSync(secretsPath)) {
    const m = fs.readFileSync(secretsPath, "utf8").match(/#define\s+DEVICE_PASSWORD\s+"([^"]+)"/);
    if (m) { devicePassword = m[1]; ok("기존 secrets.h 의 기기 비밀번호 재사용"); }
  }

  const accessRule = [
    ...OWNERS.map((e) => `(auth.token.email == '${e}' && auth.token.email_verified == true)`),
    `auth.token.email == '${DEVICE_EMAIL}'`,
  ].join(" || ");

  render(path.join(TEMPLATES, "firebase"), fbDir, {
    PROJECT_ID: PROJECT,
    SUPPORT_EMAIL: OWNERS[0],
    ACCESS_RULE: `auth != null && (${accessRule})`,
  });
  render(path.join(TEMPLATES, "RemoteFirebase"), sketchDir, {
    PROJECT_ID: PROJECT,
    WIFI_SSID: cString(WIFI_SSID),
    WIFI_PASS: cString(WIFI_PASS),
    API_KEY: cfg.apiKey,
    DATABASE_URL: db.databaseUrl,
    DEVICE_EMAIL,
    DEVICE_PASSWORD: devicePassword,
  });
  ok(`파일 생성: ${fbDir}`);
  ok(`파일 생성: ${sketchDir}`);

  // 4. 배포
  if (SKIP_DEPLOY) {
    console.log("… --skip-deploy: 배포 생략");
  } else {
    console.log("… firebase deploy --only hosting,auth,database");
    try {
      execSync(`firebase deploy --only hosting,auth,database --project ${PROJECT} --non-interactive`, { cwd: fbDir, stdio: "inherit" });
    } catch {
      fail("배포 실패. 위 로그를 확인하세요.");
    }
    ok("배포 완료");
  }

  // 5. 기기 계정
  const signUp = await postJson(`https://identitytoolkit.googleapis.com/v1/accounts:signUp?key=${cfg.apiKey}`,
    { email: DEVICE_EMAIL, password: devicePassword, returnSecureToken: false });
  if (signUp.ok) {
    ok(`기기 계정 생성: ${DEVICE_EMAIL}`);
  } else if (signUp.body.error?.message === "EMAIL_EXISTS") {
    const signIn = await postJson(`https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=${cfg.apiKey}`,
      { email: DEVICE_EMAIL, password: devicePassword, returnSecureToken: false });
    if (signIn.ok) {
      ok(`기기 계정 이미 있음, 비밀번호 일치: ${DEVICE_EMAIL}`);
    } else {
      // 비밀번호가 다르면 관리자 권한으로 secrets.h 값에 맞춘다
      const auth = { Authorization: `Bearer ${await apiv2.getAccessToken()}` };
      const base = `https://identitytoolkit.googleapis.com/v1/projects/${PROJECT}`;
      const look = await postJson(`${base}/accounts:lookup`, { email: [DEVICE_EMAIL] }, auth);
      const localId = look.body.users?.[0]?.localId;
      const upd = localId && await postJson(`${base}/accounts:update`, { localId, password: devicePassword }, auth);
      if (!upd || !upd.ok) {
        fail(`기기 계정 비밀번호를 맞추지 못했습니다. 콘솔 > Authentication > Users 에서 ${DEVICE_EMAIL} 을 지우고 다시 실행하세요.\n` +
             JSON.stringify((upd || look).body));
      }
      ok(`기기 계정 비밀번호 재설정: ${DEVICE_EMAIL}`);
    }
  } else if (signUp.body.error?.message?.startsWith("OPERATION_NOT_ALLOWED")) {
    fail("Email/Password 로그인이 꺼져 있습니다. --skip-deploy 없이 다시 실행하세요.");
  } else {
    fail(`기기 계정 생성 실패: ${JSON.stringify(signUp.body)}`);
  }

  console.log(`
완료
  웹페이지 : https://${PROJECT}.web.app
  스케치   : ${sketchDir}
  업로드   : arduino-cli compile --upload -p <포트> --fqbn esp32:esp32:XIAO_ESP32S3 "${sketchDir}"`);
  process.exit(0);
})().catch((e) => fail(`${e.message}${e.original ? " / " + e.original.message : ""}`));
