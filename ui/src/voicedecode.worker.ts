const RATE = 48000
const ENV_STEP = 0.025
const WARM = 15
const QUEUE = 32

type Job = { index: number; frames: Uint8Array[] }

const frameSeconds = (toc: number) => {
  const config = toc >> 3
  if (config < 12) return [0.01, 0.02, 0.04, 0.06][config & 3]
  if (config < 16) return [0.01, 0.02][config & 1]
  return [0.0025, 0.005, 0.01, 0.02][config & 3]
}

let speakers: Job[][] = []
const where = new Map<number, [number, number]>()

const room = (decoder: AudioDecoder) =>
  decoder.decodeQueueSize < QUEUE ? Promise.resolve() : new Promise(resolve => decoder.addEventListener('dequeue', resolve, { once: true }))

const run = async (jobs: Job[], skip: number, done: (job: Job, pcm: Float32Array<ArrayBuffer>) => void) => {
  const pending: { job: Job; chunks: Float32Array[]; left: number }[] = []
  const order: number[] = []
  let failed: unknown = null
  const decoder = new AudioDecoder({
    output: data => {
      const at = order.shift()!
      const p = pending[at]
      const pcm = new Float32Array(data.numberOfFrames)
      data.copyTo(pcm, { planeIndex: 0, format: 'f32-planar' })
      data.close()
      p.chunks.push(pcm)
      if (--p.left > 0) return
      const total = p.chunks.reduce((n, c) => n + c.length, 0)
      const out = new Float32Array(Math.max(total, 1))
      let o = 0
      for (const c of p.chunks) {
        out.set(c, o)
        o += c.length
      }
      p.chunks = []
      if (at >= skip) done(p.job, out)
    },
    error: e => {
      failed = e
    },
  })
  decoder.configure({ codec: 'opus', sampleRate: RATE, numberOfChannels: 1 })
  let ts = 0
  for (const job of jobs) {
    const index = pending.push({ job, chunks: [], left: job.frames.length }) - 1
    for (const frame of job.frames) {
      await room(decoder)
      order.push(index)
      decoder.decode(new EncodedAudioChunk({ type: 'key', timestamp: ts, data: frame }))
      ts += Math.round(frameSeconds(frame[0]) * 1e6)
    }
  }
  await decoder.flush()
  decoder.close()
  if (failed) throw failed
}

const envRun = async (jobs: Job[]) => {
  const step = Math.round(ENV_STEP * RATE)
  const state = jobs.map(job => ({ left: job.frames.length, sum: 0, n: 0, env: [] as number[] }))
  const order: number[] = []
  let scratch = new Float32Array(0)
  let failed: unknown = null
  const decoder = new AudioDecoder({
    output: data => {
      const at = order.shift()!
      const st = state[at]
      const len = data.numberOfFrames
      if (scratch.length < len) scratch = new Float32Array(len)
      const pcm = scratch.subarray(0, len)
      data.copyTo(pcm, { planeIndex: 0, format: 'f32-planar' })
      data.close()
      for (let k = 0; k < len; k++) {
        st.sum += pcm[k] * pcm[k]
        if (++st.n === step) {
          st.env.push(Math.sqrt(st.sum / step))
          st.sum = 0
          st.n = 0
        }
      }
      if (--st.left > 0) return
      if (st.n > 0 || !st.env.length) st.env.push(Math.sqrt(st.sum / Math.max(1, st.n)))
      const env = Float32Array.from(st.env)
      st.env = []
      postMessage({ index: jobs[at].index, env }, { transfer: [env.buffer] })
    },
    error: e => {
      failed = e
    },
  })
  decoder.configure({ codec: 'opus', sampleRate: RATE, numberOfChannels: 1 })
  let ts = 0
  for (const [at, job] of jobs.entries()) {
    for (const frame of job.frames) {
      await room(decoder)
      order.push(at)
      decoder.decode(new EncodedAudioChunk({ type: 'key', timestamp: ts, data: frame }))
      ts += Math.round(frameSeconds(frame[0]) * 1e6)
    }
  }
  await decoder.flush()
  decoder.close()
  if (failed) throw failed
}

const envelopes = async () => {
  await Promise.all(speakers.map(envRun))
  postMessage({ envs: true })
}

const warmStart = (jobs: Job[], first: number) => {
  let k = first
  let sec = 0
  while (k > 0 && sec < WARM) {
    k--
    for (const f of jobs[k].frames) sec += frameSeconds(f[0])
  }
  return k
}

const load = async (id: number, indices: number[]) => {
  const bySpeaker = new Map<number, number[]>()
  for (const i of indices) {
    const w = where.get(i)
    if (w) bySpeaker.set(w[0], [...(bySpeaker.get(w[0]) ?? []), w[1]])
  }
  await Promise.all(
    [...bySpeaker].map(async ([speaker, list]) => {
      const jobs = speakers[speaker]
      list.sort((a, b) => a - b)
      const runs: [number, number][] = []
      for (const at of list) {
        const last = runs[runs.length - 1]
        if (last && warmStart(jobs, at) <= last[1] + 1) last[1] = at
        else runs.push([at, at])
      }
      for (const [first, end] of runs) {
        const from = warmStart(jobs, first)
        await run(jobs.slice(from, end + 1), first - from, (job, pcm) => postMessage({ index: job.index, pcm }, { transfer: [pcm.buffer] }))
      }
    }),
  )
  postMessage({ loaded: id })
}

onmessage = (e: MessageEvent<{ speakers?: Job[][]; load?: number; indices?: number[] }>) => {
  const m = e.data
  if (m.speakers) {
    speakers = m.speakers
    speakers.forEach((jobs, s) => jobs.forEach((job, k) => where.set(job.index, [s, k])))
    envelopes().catch(err => postMessage({ error: String(err) }))
  } else if (m.load !== undefined) {
    load(m.load, m.indices ?? []).catch(err => postMessage({ error: String(err), loaded: m.load }))
  }
}
