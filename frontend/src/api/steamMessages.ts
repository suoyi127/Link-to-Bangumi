import { ApiError } from './request'

const steamMessages: Record<string, string> = {
  steam_login_helper_unavailable: '当前环境无法打开 Steam 登录窗口，请展开 Steam 导入设置手动填写。',
  steam_login_in_progress: 'Steam 登录正在进行，请在登录窗口完成操作。',
  steam_login_failed: 'Steam 自动配置失败，请重试或展开 Steam 导入设置手动填写。',
  steam_login_not_found: 'Steam 登录会话已失效，请重新登录。',
  invalid_steam_config: 'Steam 设置格式不正确，请检查 Steam ID 和 API Key。',
  invalid_steam_path: 'Steam 安装目录无效，请填写本机实际安装目录。',
  steam_not_found: '未找到 Steam 安装目录，请展开设置填写实际路径。',
  steam_import_in_progress: 'Steam 导入正在进行，请等待完成。',
  steam_import_failed: 'Steam 导入失败，请检查安装目录后重试。',
  steam_config_storage_failed: 'Steam 设置保存失败，请重试。',
  steam_credentials_required: '未配置 Steam ID 或 API Key，已导入本机游戏，统计暂不可用。',
  steam_stats_unavailable: '部分 Steam 统计未能获取，已获取的数据仍会保留。可稍后重新导入；持续失败时再检查游戏详情隐私设置和网络连接。',
  steam_stats_not_supported: '部分游戏未提供 Steam 统计数据，成就数量暂无法获取；已导入的游戏和游玩时长不受影响。',
  steam_rate_limited: 'Steam 请求过于频繁，请稍后重新导入以同步统计。',
  steam_stats_pending: 'Steam 统计尚未同步。',
  steam_metadata_unavailable: '部分游戏的 Steam 商店资料暂不可用，已保留原名称和已有封面，可稍后重新导入补齐。',
  steam_cover_unavailable: '部分游戏的 Steam 封面暂未获取成功，已保留已有封面，可稍后重新导入补齐。',
  steam_private_profile: 'Steam 个人资料或游戏详情未公开。',
}
export const steamWarningText = (code: string) => steamMessages[code] || code
export const steamErrorText = (error: unknown) => error instanceof ApiError ? (steamMessages[error.code] || error.message) : error instanceof Error ? error.message : 'Steam 请求失败'
