import { plural, type Lang, type T } from './i18n'

export const masks = (slots: Iterable<number>) => {
  let low = 0
  let high = 0
  for (const s of slots) {
    if (s < 0 || s > 63) continue
    if (s < 32) low |= 1 << s
    else high |= 1 << (s - 32)
  }
  return { low, high }
}

export const command = (slots: Iterable<number>) => {
  const { low, high } = masks(slots)
  return high ? `tv_listen_voice_indices ${low}; tv_listen_voice_indices_h ${high}` : `tv_listen_voice_indices ${low}`
}

export const source = (server: string, t: T) => {
  const s = server.toLowerCase()
  if (s.includes('faceit')) return 'FACEIT'
  if (s.includes('valve')) return t('matchmaking')
  return server ? t('community') : ''
}

type Member = { name: string; steamid: string }

const teamName = (raw: string, members: Member[], me: string, meElsewhere: boolean, t: T) => {
  const name = raw.replace(/^team_/i, '').trim()
  if (name) return name
  if (members.some(p => p.steamid === me)) return t('yourTeam')
  if (meElsewhere) return t('opponents')
  return members[0] ? t('teamOf', { name: members[0].name }) : t('emptyTeam')
}

export const teamLabels = (r: { teams: { name: string }[]; players: (Member & { team: number })[] }, me: string, t: T) => {
  const members = [0, 1].map(i => r.players.filter(p => p.team === i))
  return [0, 1].map(i => teamName(r.teams[i]?.name ?? '', members[i], me, members[1 - i].some(p => p.steamid === me), t))
}

export const sideName = (side: number) => (side === 2 ? 'T' : side === 3 ? 'CT' : '')

export const mapName = (map: string, t: T) => {
  const m = map.replace(/^(de|cs|ar|dz)_/, '')
  return m ? m[0].toUpperCase() + m.slice(1) : t('unknownMap')
}

export const when = (unix: number, t: T, lang: Lang) => {
  const d = new Date(unix * 1000)
  const now = new Date()
  const time = d.toLocaleTimeString(lang, { hour: '2-digit', minute: '2-digit' })
  const day = (x: Date) => new Date(x.getFullYear(), x.getMonth(), x.getDate()).getTime()
  const diff = Math.round((day(now) - day(d)) / 86400000)
  if (diff === 0) return t('today', { t: time })
  if (diff === 1) return t('yesterday', { t: time })
  return `${d.toLocaleDateString(lang, { day: 'numeric', month: 'short' })}, ${time}`
}

export const length = (seconds: number, t: T) => t('minutes', { n: Math.floor(seconds / 60) })

export const rounds = (n: number, t: T) => plural(t, n, 'roundsOne', 'roundsFew', 'roundsMany')
