export function hasSuccessfulJobs(run, jobs, requiredJobNames) {
  return run.status === 'completed'
    && requiredJobNames.every((requiredName) => jobs.some((job) => job.name === requiredName
      && job.status === 'completed'
      && job.conclusion === 'success'));
}

export function isVerifiedWasmProducer(run, jobs) {
  return hasSuccessfulJobs(run, jobs, ['wasm / test']);
}
