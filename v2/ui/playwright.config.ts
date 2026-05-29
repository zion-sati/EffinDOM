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
      },
    },
  ],
  reporter: [
    ['list'],
    ['html', { outputFolder: path.join(__dirname, 'tests', 'integration', 'report'), open: 'never' }],
  ],
});
