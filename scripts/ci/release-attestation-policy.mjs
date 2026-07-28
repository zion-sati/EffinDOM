export function isVerifiedWasmProducer(run, jobs) {
  return run.status === 'completed'
    && jobs.some((job) => job.name === 'wasm / test'
      && job.status === 'completed'
      && job.conclusion === 'success');
}
