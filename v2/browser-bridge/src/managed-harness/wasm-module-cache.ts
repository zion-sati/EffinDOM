import { getHostServiceImportNames, type HostServicesDefinition } from './host-services';

/** Application modules by path, compiled once per page. Only the compiled module is kept, not its bytes. */
export class WasmModuleCache {
  private readonly wasmModuleCache = new Map<string, Promise<WebAssembly.Module>>();

  async loadWasmModule(wasmPath: string): Promise<WebAssembly.Module> {
    const cached = this.wasmModuleCache.get(wasmPath);
    if (cached !== undefined) {
      return cached;
    }
    const compilePromise = fetch(wasmPath, { cache: 'no-store' }).then(async (response) => {
      if (!response.ok) {
        throw new Error(`Failed to load wasm app: ${wasmPath}`);
      }
      return await WebAssembly.compile(await response.arrayBuffer());
    });
    this.wasmModuleCache.set(wasmPath, compilePromise);
    return compilePromise;
  }
}

export function validateAppImports(wasmModule: WebAssembly.Module, hostServices: HostServicesDefinition | undefined): void {
  const allowedHostServiceImports = getHostServiceImportNames(hostServices);
  for (const imported of WebAssembly.Module.imports(wasmModule)) {
    if (imported.kind !== 'function') {
      throw new Error(`App import ${imported.module}.${imported.name} is not allowed.`);
    }
    if (imported.module === 'effindom_v2_ui' || imported.module === 'fui_host' || imported.module === 'env') {
      continue;
    }
    if (imported.module === 'fui_host_service' && allowedHostServiceImports.has(imported.name)) {
      continue;
    }
    throw new Error(`App import ${imported.module}.${imported.name} is not allowed.`);
  }
}
