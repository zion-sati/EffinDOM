import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";

const buildScript = readFileSync(
  new URL("../build-native-runtime.mjs", import.meta.url),
  "utf8",
);

test("native runtime builds remain demo-free unless package acceptance opts in", () => {
  assert.match(buildScript, /const withDemo = args\.includes\('--with-demo'\);/);
  assert.match(
    buildScript,
    /`-DEFFINDOM_BUILD_NATIVE_FUI_RS_DEMO=\$\{withDemo \? 'ON' : 'OFF'\}`/,
  );
});
