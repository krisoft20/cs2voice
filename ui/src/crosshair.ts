const DICT = 'ABCDEFGHJKLMNOPQRSTUVWXYZabcdefhijkmnopqrstuvwxyz23456789'

export type Rgba = [number, number, number, number]

export type Crosshair = {
  format: 'cs2' | 'pixel' | 'classic'
  style: number
  color: Rgba
  outline: Rgba
  outlineMode: number
  thickness: number
  length: number
  gap: number
  dot: boolean
  tStyle: boolean
  screen: number
}

const CLASSIC_COLORS: Rgba[] = [
  [250, 50, 50, 255],
  [50, 250, 50, 255],
  [250, 250, 50, 255],
  [50, 50, 250, 255],
  [50, 250, 250, 255],
]

const toBytes = (body: string, n: number) => {
  let v = 0n
  for (let i = body.length - 1; i >= 0; i--) {
    const d = DICT.indexOf(body[i])
    if (d < 0) return null
    v = v * 57n + BigInt(d)
  }
  const out = new Array<number>(n)
  for (let i = n - 1; i >= 0; i--) {
    out[i] = Number(v & 255n)
    v >>= 8n
  }
  if (v !== 0n) return null
  let sum = 0
  for (let i = 1; i < n; i++) sum += out[i]
  return sum % 256 === out[0] ? out : null
}

const int8 = (b: number) => (b > 127 ? b - 256 : b)

export const decode = (code: string): Crosshair | null => {
  if (/^CS[A-Za-z0-9]{44}$/.test(code)) {
    const b = toBytes(code.slice(2), 32)
    if (!b || b[1] !== 1) return null
    const gap = b[14] | (b[15] << 8)
    return {
      format: 'cs2',
      screen: b[2] | (b[3] << 8),
      style: b[4] & 31,
      dot: !!(b[4] & 64),
      tStyle: !!(b[4] & 128),
      color: [b[5], b[6], b[7], b[8]],
      outline: [b[9], b[10], b[11], b[12]],
      thickness: b[13] & 63,
      outlineMode: b[13] >> 6,
      gap: gap > 32767 ? gap - 65536 : gap,
      length: b[16],
    }
  }
  const m = /^CSGO((?:-[A-Za-z0-9]{5}){5})$/.exec(code)
  if (!m) return null
  const b = toBytes(m[1].replace(/-/g, ''), 18)
  if (!b) return null
  if (b[1] === 3 || b[1] === 4) {
    const bits = (b[10] | (b[11] << 8) | (b[12] << 16) | (b[13] << 24)) >>> 0
    return {
      format: 'pixel',
      screen: b[14] | (b[15] << 8),
      style: b[2] & 15,
      dot: !!(b[2] & 64),
      tStyle: !!(b[2] & 128),
      color: [b[3], b[4], b[5], b[6]],
      outline: [0, 0, 0, 255],
      thickness: (bits >>> 23) & 31,
      outlineMode: b[1] === 4 ? (b[13] >> 4) & 3 : b[2] & 32 ? 1 : 0,
      gap: b[7],
      length: b[8],
    }
  }
  if (b[1] !== 1) return null
  const index = b[10] & 7
  return {
    format: 'classic',
    screen: 0,
    style: (b[13] & 15) >> 1,
    dot: !!((b[13] >> 4) & 1),
    tStyle: !!((b[13] >> 4) & 8),
    color: index === 5 ? [b[4], b[5], b[6], (b[13] >> 4) & 4 ? b[7] : 255] : CLASSIC_COLORS[index] ?? CLASSIC_COLORS[1],
    outline: [0, 0, 0, 255],
    thickness: b[12] / 10,
    outlineMode: b[10] & 8 ? 1 : 0,
    gap: int8(b[2]) / 10,
    length: b[14] / 10,
  }
}

type Box = [number, number, number, number]

const shapes = (x: Crosshair, cx: number, cy: number): Box[] => {
  const t = Math.max(1, x.thickness)
  const h = Math.floor(t / 2)
  const g = x.gap
  const l = x.length
  const lo = cx - h
  const out: Box[] = []
  if (x.style === 6) return [[lo, cy - h, t, t]]
  if (x.style === 7 || x.style === 8 || x.style === 9) {
    const r = g + l
    const left = lo - r - t
    const top = cy - h - r - t
    const side = 2 * r + 3 * t
    const far = side - t
    if (x.style === 8) {
      out.push([left, top, side, t], [left, top + far, side, t], [left, top, t, side], [left + far, top, t, side])
    } else {
      const arm = l + t
      for (const [ox, oy] of [[0, 0], [1, 0], [0, 1], [1, 1]]) {
        const ex = left + ox * far
        const ey = top + oy * far
        out.push([ox ? ex + t - arm : ex, ey, arm, t], [ex, oy ? ey + t - arm : ey, t, arm])
      }
    }
  } else if (l > 0) {
    out.push([lo + t + g, cy - h, l, t])
    out.push([lo - g - l, cy - h, l, t])
    out.push([lo, cy - h + t + g, t, l])
    if (!x.tStyle) out.push([lo, cy - h - g - l, t, l])
  }
  if (x.dot) out.push([lo, cy - h, t, t])
  return out
}

const extent = (x: Crosshair) => Math.max(4, Math.abs(x.gap) + x.length + Math.max(1, x.thickness) + 2)

const rgba = (c: Rgba) => `rgba(${c[0]},${c[1]},${c[2]},${(c[3] / 255).toFixed(3)})`

export const draw = (canvas: HTMLCanvasElement, x: Crosshair) => {
  const size = 2 * extent(x) + 3
  canvas.width = size
  canvas.height = size
  const ctx = canvas.getContext('2d')
  if (!ctx) return
  ctx.clearRect(0, 0, size, size)
  const c = Math.floor(size / 2)
  const circle = x.style === 1 || x.style === 3
  const t = Math.max(1, x.thickness)
  if (circle) {
    const r = x.gap + x.length / 2 + t / 2
    if (x.outlineMode) {
      ctx.strokeStyle = rgba(x.outline)
      ctx.lineWidth = t + 2
      ctx.beginPath()
      ctx.arc(c + 0.5, c + 0.5, r, 0, Math.PI * 2)
      ctx.stroke()
    }
    ctx.strokeStyle = rgba(x.color)
    ctx.lineWidth = t
    ctx.beginPath()
    ctx.arc(c + 0.5, c + 0.5, r, 0, Math.PI * 2)
    ctx.stroke()
    if (x.dot) {
      ctx.fillStyle = rgba(x.color)
      ctx.fillRect(c - Math.floor(t / 2), c - Math.floor(t / 2), t, t)
    }
    return
  }
  const boxes = shapes(x, c, c)
  if (x.outlineMode) {
    ctx.fillStyle = rgba(x.outline)
    for (const [bx, by, bw, bh] of boxes) {
      if (x.outlineMode === 2) ctx.fillRect(bx + 1, by + 1, bw, bh)
      else ctx.fillRect(bx - 1, by - 1, bw + 2, bh + 2)
    }
  }
  ctx.fillStyle = rgba(x.color)
  for (const [bx, by, bw, bh] of boxes) ctx.fillRect(bx, by, bw, bh)
}
