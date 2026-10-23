import { createRoot } from 'react-dom/client'
import '@fontsource/barlow-condensed/500.css'
import '@fontsource/barlow-condensed/600.css'
import '@fontsource/barlow-semi-condensed/400.css'
import '@fontsource/barlow-semi-condensed/500.css'
import './styles.css'
import App from './App'

createRoot(document.getElementById('root')!).render(<App />)
