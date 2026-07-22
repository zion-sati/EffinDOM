import * as path from 'node:path';

import { defineConfig } from '@playwright/test';

export default defineConfig({
  testDir: path.join(__dirname, 'tests', 'integration'),
  testMatch: '**/*.spec.ts',
  timeout: 60_000,
  retries: 1,
  use: {
    headless: true,
  },
  projects: [
    {
      name: 'chromium',
      use: {
        browserName: 'chromium',
        launchOptions: {
          // Headless Chromium's default Linux ANGLE selection can expose a
          // WebGL2 context that immediately loses its stencil attachment.
          // Select Chromium's supported software GL backend explicitly so
          // the renderer smoke lane exercises a stable WebGL2 device.
          args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader'],
        },
      },
    },
  ],
  reporter: [
    ['list'],
    ['html', { outputFolder: path.join(__dirname, 'tests', 'integration', 'report'), open: 'never' }],
  ],
});
