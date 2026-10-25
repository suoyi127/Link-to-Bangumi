import { Button, InputNumber } from 'antd'
import { SettingsSection } from './SettingsSection'
import type { usePreferences } from './usePreferences'
import type { Preferences } from '../../api/types'

export function PreferencesSection({ controller }: { controller: ReturnType<typeof usePreferences> }) {
  const { draft, setDraft, saving, save } = controller
  if (!draft) return null
  return <SettingsSection title="偏好">
      <label>首选整理方式 <select aria-label="首选整理方式" value={draft.preferredOperation} onChange={(event) => setDraft({ ...draft, preferredOperation: event.target.value as Preferences['preferredOperation'] })}><option value="hardlink">硬链接</option><option value="copy">复制</option><option value="symlink">符号链接</option></select></label>
      <label>扫描间隔（预留，qB 每 10 秒检测下载完成） <InputNumber aria-label="扫描间隔（秒）" min={60} max={86400} value={draft.scanIntervalSeconds} onChange={(value) => setDraft({ ...draft, scanIntervalSeconds: value ?? 3600 })} /></label>
      <Button type="primary" loading={saving} onClick={() => void save()}>保存偏好</Button>
      </SettingsSection>
}
