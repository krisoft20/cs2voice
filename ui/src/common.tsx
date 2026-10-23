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

export function Avatar(props: { url?: string; name: string }) {
  const [broken, setBroken] = useState('')
  if (props.url && broken !== props.url) return <img className="avatar" src={props.url} alt="" onError={() => setBroken(props.url ?? '')} />
  return <span className="avatar none">{(props.name || '?').slice(0, 1).toUpperCase()}</span>
}
