import { createContext, useContext, useState } from 'react'
import { translator, type Lang, type T } from './i18n'

export type Loader = 'plant' | 'radar' | 'rounds' | 'ammo'

type Ctx = { t: T; lang: Lang; me: string; loader: Loader }

export const UiContext = createContext<Ctx>({ t: translator('en'), lang: 'en', me: '', loader: 'plant' })
export const useUi = () => useContext(UiContext)

export const remembered = (key: string, fallback: string) => {
  try {
    return localStorage.getItem(key) ?? fallback
  } catch {
    return fallback
  }
}

export const remember = (key: string, value: string) => {
  try {
    localStorage.setItem(key, value)
  } catch {}
}

const PLAYER_COLORS = ['#f2e43a', '#a54ad8', '#21b26c', '#3e9de8', '#f08f26']

export const playerColor = (c?: number) => (c !== undefined && c >= 0 ? PLAYER_COLORS[c] : undefined)

export const colorStyle = (c?: number) => {
  const pc = playerColor(c)
  return pc ? { ['--pc' as string]: pc } : undefined
}

export function Avatar(props: { url?: string; name: string; color?: number }) {
  const [broken, setBroken] = useState('')
  const pc = playerColor(props.color) ? ' pc' : ''
  const style = colorStyle(props.color)
  if (props.url && broken !== props.url) return <img className={`avatar${pc}`} style={style} src={props.url} alt="" onError={() => setBroken(props.url ?? '')} />
  return <span className={`avatar none${pc}`} style={style}>{(props.name || '?').slice(0, 1).toUpperCase()}</span>
}
