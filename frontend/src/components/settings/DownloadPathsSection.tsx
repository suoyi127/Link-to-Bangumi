import { Alert, Button, Descriptions, Input, Typography } from 'antd'
import { SettingsSection } from './SettingsSection'
import type { useDownloadPaths } from './useDownloadPaths'
import type { Settings } from '../../api/types'

export function DownloadPathsSection({ settings, qbPathPending, controller }: { settings: Settings; qbPathPending: boolean; controller: ReturnType<typeof useDownloadPaths> }) {
  const { qbPath, setQbPath, qbPathSaving, desktopRestarting, saveQbPath } = controller
  return <SettingsSection title="生效路径" wide>
      <Descriptions bordered column={1} items={[
        { key: 'source', label: '当前 qB 下载目录', children: settings.qbDownloadConfigured ? settings.sourcePath : '尚未配置 qB 下载目录' },
        { key: 'import', label: '外来导入目录', children: settings.importPath },
        { key: 'library', label: '媒体库目录', children: settings.libraryPath },
        { key: 'data', label: '数据目录', children: settings.dataPath },
      ]} />
      <label>qB 下载目录 <Input aria-label="qB 下载目录" value={qbPath} onChange={(event) => setQbPath(event.target.value)} maxLength={4096} placeholder="选择已有的 qB 下载文件夹路径" /></label>
      <Typography.Text type="secondary">仅识别你选择的 qB 下载目录；外来导入与媒体库保持独立。修改目录不会移动或删除已有文件。</Typography.Text>
      <Button loading={qbPathSaving} disabled={settings.qbDownloadEnvironmentOverride} onClick={() => void saveQbPath()}>保存 qB 下载目录</Button>
      {settings.qbDownloadEnvironmentOverride && <Alert type="info" message="环境变量正在覆盖页面保存的 qB 下载目录；移除 ANIME_VAULT_SOURCE_DIR 后才能在此修改。" />}
      {qbPathPending && <Alert type="warning" message={desktopRestarting ? 'qB 下载目录已保存，正在自动重启后端并刷新页面…' : 'qB 下载目录已保存，重启后端后生效。'} />}
      </SettingsSection>
}
