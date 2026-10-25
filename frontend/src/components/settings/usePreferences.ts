import { useCallback, useState } from 'react'
import { putSettings } from '../../api/settings'
import type { Preferences, Settings } from '../../api/types'
import { errorText, useRequestScope } from './useRequestScope'

function preferencesOf(settings: Settings): Preferences {
  return {
    preferredOperation: settings.preferredOperation, scanIntervalSeconds: settings.scanIntervalSeconds,
    mpvExecutable: settings.mpvExecutable, qbWebUiUrl: settings.qbWebUiUrl,
  }
}

export function usePreferences(onSaved: (settings: Settings) => void) {
  const [draft, setDraft] = useState<Preferences | null>(null)
  const [saving, setSaving] = useState(false)
  const [error, setError] = useState('')
  const requests = useRequestScope()
  const initialize = useCallback((settings: Settings) => setDraft(preferencesOf(settings)), [])

  async function save() {
    if (!draft || saving) return
    const request = requests.start()
    setSaving(true)
    setError('')
    try {
      const updated = await putSettings(draft)
      if (!request.isCurrent()) return
      onSaved(updated)
      setDraft(preferencesOf(updated))
    } catch (cause) {
      if (request.isCurrent()) setError(errorText(cause))
    } finally {
      if (request.isCurrent()) setSaving(false)
    }
  }

  return { draft, setDraft, saving, error, initialize, save }
}
