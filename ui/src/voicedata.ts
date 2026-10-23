const MAGIC = 0x56325343
const RATE = 48000
const GAP = 0.3

export type Round = { start: number; live: number; end: number }
export type Kill = { at: number; killer: number; victim: number; head: boolean }

export type Spurt = { speaker: number; start: number; end: number; frames: Uint8Array[]; audio?: AudioBuffer; env?: Float32Array }

export const ENV_STEP = 0.025

export type Voice = {
  length: number
  speakers: string[]
  rounds: Round[]
  kills: Kill[]
  spurts: Spurt[]
}

const frameSeconds = (toc: number) => {
  const config = toc >> 3
  if (config < 12) return [0.01, 0.02, 0.04, 0.06][config & 3]
  if (config < 16) return [0.01, 0.02][config & 1]
  return [0.0025, 0.005, 0.01, 0.02][config & 3]
}

export const parse = (buf: ArrayBuffer): Voice | null => {
  const view = new DataView(buf)
  if (buf.byteLength < 12 || view.getUint32(0, true) !== MAGIC) return null
  const headLen = view.getUint32(8, true)
  const head = JSON.parse(new TextDecoder().decode(new Uint8Array(buf, 12, headLen))) as {
    tick_interval: number
    ticks: number
    speakers: string[]
    rounds: [number, number, number][]
    kills: [number, number, number, number][]
  }
  const dt = head.tick_interval
  const sec = (tick: number) => tick * dt
  let at = 12 + headLen
  const count = view.getUint32(at, true)
  at += 4
  const open: (Spurt | null)[] = head.speakers.map(() => null)
  const spurts: Spurt[] = []
  for (let i = 0; i < count; i++) {
    const speaker = view.getUint8(at)
    const tick = view.getInt32(at + 1, true)
    const len = view.getUint16(at + 5, true)
    const frame = new Uint8Array(buf, at + 7, len)
    at += 7 + len
    const t = sec(tick)
    let s = open[speaker]
    if (!s || t > s.end + GAP) {
      s = { speaker, start: t, end: t, frames: [] }
      open[speaker] = s
      spurts.push(s)
    }
    s.frames.push(frame)
    s.end += frameSeconds(frame[0])
  }
  return {
    length: sec(head.ticks),
    speakers: head.speakers,
    rounds: head.rounds.map(([start, live, end]) => ({ start: sec(start), live: live < 0 ? sec(start) : sec(live), end: sec(end) })),
    kills: head.kills.map(([tick, killer, victim, head]) => ({ at: sec(tick), killer, victim, head: !!head })),
    spurts,
  }
}

const decodeSpeaker = async (spurts: Spurt[], ctx: BaseAudioContext, each: (s: Spurt) => void) => {
  const pending: { spurt: Spurt; chunks: Float32Array[]; left: number }[] = []
  const order: number[] = []
  let failed: unknown = null
  const decoder = new AudioDecoder({
    output: data => {
      const job = pending[order.shift()!]
      const pcm = new Float32Array(data.numberOfFrames)
      data.copyTo(pcm, { planeIndex: 0, format: 'f32-planar' })
      data.close()
      job.chunks.push(pcm)
      if (--job.left > 0) return
      const total = job.chunks.reduce((n, c) => n + c.length, 0)
      const audio = ctx.createBuffer(1, Math.max(total, 1), RATE)
      const out = audio.getChannelData(0)
      let o = 0
      for (const c of job.chunks) {
        out.set(c, o)
        o += c.length
      }
      job.spurt.audio = audio
      const step = Math.round(ENV_STEP * RATE)
      const env = new Float32Array(Math.max(1, Math.ceil(out.length / step)))
      for (let b = 0; b < env.length; b++) {
        let sum = 0
        const end = Math.min(out.length, (b + 1) * step)
        for (let k = b * step; k < end; k++) sum += out[k] * out[k]
        env[b] = Math.sqrt(sum / Math.max(1, end - b * step))
      }
      job.spurt.env = env
      job.chunks = []
      each(job.spurt)
    },
    error: e => {
      failed = e
    },
  })
  decoder.configure({ codec: 'opus', sampleRate: RATE, numberOfChannels: 1 })
  let ts = 0
  for (const spurt of spurts) {
    const index = pending.push({ spurt, chunks: [], left: spurt.frames.length }) - 1
    for (const frame of spurt.frames) {
      order.push(index)
      decoder.decode(new EncodedAudioChunk({ type: 'key', timestamp: ts, data: frame }))
      ts += Math.round(frameSeconds(frame[0]) * 1e6)
    }
  }
  await decoder.flush()
  decoder.close()
  if (failed) throw failed
}

export const decode = async (voice: Voice, ctx: BaseAudioContext, each: (s: Spurt) => void) => {
  await Promise.all(
    voice.speakers.map((_, i) =>
      decodeSpeaker(
        voice.spurts.filter(s => s.speaker === i),
        ctx,
        each,
      ),
    ),
  )
}

const toBase64 = (bytes: Uint8Array) => {
  let out = ''
  const CHUNK = 0x8000
  for (let i = 0; i < bytes.length; i += CHUNK) out += String.fromCharCode(...bytes.subarray(i, i + CHUNK))
  return btoa(out)
}

const wav = (pcm: Float32Array) => {
  const buf = new ArrayBuffer(44 + pcm.length * 2)
  const v = new DataView(buf)
  const text = (at: number, s: string) => [...s].forEach((c, i) => v.setUint8(at + i, c.charCodeAt(0)))
  text(0, 'RIFF')
  v.setUint32(4, 36 + pcm.length * 2, true)
  text(8, 'WAVE')
  text(12, 'fmt ')
  v.setUint32(16, 16, true)
  v.setUint16(20, 1, true)
  v.setUint16(22, 1, true)
  v.setUint32(24, RATE, true)
  v.setUint32(28, RATE * 2, true)
  v.setUint16(32, 2, true)
  v.setUint16(34, 16, true)
  text(36, 'data')
  v.setUint32(40, pcm.length * 2, true)
  for (let i = 0; i < pcm.length; i++) v.setInt16(44 + i * 2, Math.round(Math.max(-1, Math.min(1, pcm[i])) * 32767), true)
  return new Uint8Array(buf)
}

export const renderClip = async (voice: Voice, from: number, to: number, audible: (speaker: number) => boolean) => {
  const length = Math.max(1, Math.round((to - from) * RATE))
  const ctx = new OfflineAudioContext(1, length, RATE)
  for (const s of voice.spurts) {
    if (!s.audio || !audible(s.speaker) || s.end <= from || s.start >= to) continue
    const src = ctx.createBufferSource()
    src.buffer = s.audio
    src.connect(ctx.destination)
    src.start(Math.max(0, s.start - from), Math.max(0, from - s.start))
  }
  const out = await ctx.startRendering()
  return toBase64(wav(out.getChannelData(0)))
}
