import { expect, test } from '@playwright/test';

import { createWorkerManager } from '../src/managed-harness/worker-manager';
import type { WorkerBootstrapOutboundMessage } from '../src/managed-harness/worker-types';

class FakeWorker {
  static instances: FakeWorker[] = [];

  readonly posted: unknown[] = [];
  terminated = false;
  private readonly listeners = new Map<string, ((event: never) => void)[]>();

  constructor(readonly url: string) {
    FakeWorker.instances.push(this);
  }

  addEventListener(type: string, listener: (event: never) => void): void {
    const listeners = this.listeners.get(type) ?? [];
    listeners.push(listener);
    this.listeners.set(type, listeners);
  }

  postMessage(message: unknown): void {
    this.posted.push(message);
  }

  terminate(): void {
    this.terminated = true;
  }

  emit(message: WorkerBootstrapOutboundMessage): void {
    for (const listener of this.listeners.get('message') ?? []) {
      listener({ data: message } as never);
    }
  }
}

test('worker manager preserves ordering, cancellation, and one terminal event', () => {
  const workerDescriptor = Object.getOwnPropertyDescriptor(globalThis, 'Worker');
  Object.defineProperty(globalThis, 'Worker', { configurable: true, value: FakeWorker });
  FakeWorker.instances = [];
  try {
    const events: string[] = [];
    let callbackText = '';
    const manager = createWorkerManager({
      scriptBaseUrl: 'https://example.test/app/',
      getCurrentSession: () => ({
        memory: new WebAssembly.Memory({ initial: 1 }),
        textBufferPtr: 64,
        textBufferSize: 1024,
        exports: {
          __fui_on_worker_progress: () => events.push(`progress:${callbackText}`),
          __fui_on_worker_complete: () => events.push(`complete:${callbackText}`),
          __fui_on_worker_error: () => events.push(`error:${callbackText}`),
        },
      }),
      getCurrentWorkerHostServices: () => undefined,
      writeTextCallbackPayload: (_session, text) => {
        callbackText = text;
        return new TextEncoder().encode(text).length;
      },
    });

    manager.startString(7, './workers.wasm', 'findPrimes', 'input');
    const worker = FakeWorker.instances[0];
    expect(worker).toBeDefined();
    expect(worker?.posted).toEqual([{
      type: 'start',
      workerId: 7,
      wasmUrl: 'https://example.test/app/workers.wasm',
      entryName: 'findPrimes',
      input: 'input',
    }]);
    worker?.emit({ type: 'progress', workerId: 7, text: 'one' });
    manager.cancel(7);
    worker?.emit({ type: 'progress', workerId: 7, text: 'ignored' });
    worker?.emit({ type: 'complete', workerId: 7, text: 'done' });
    worker?.emit({ type: 'error', workerId: 7, text: 'late' });

    expect(events).toEqual(['progress:one', 'complete:done']);
    expect(worker?.posted[worker.posted.length - 1]).toEqual({ type: 'cancel', workerId: 7 });
    expect(worker?.terminated).toBe(true);
  } finally {
    if (workerDescriptor === undefined) {
      Reflect.deleteProperty(globalThis, 'Worker');
    } else {
      Object.defineProperty(globalThis, 'Worker', workerDescriptor);
    }
  }
});
