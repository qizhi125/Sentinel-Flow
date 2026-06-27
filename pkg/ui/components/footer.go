// Sentinel-Flow NIDS — 页脚组件。
package components

import "github.com/gizak/termui/v3/widgets"

// NewFooter 创建底部页脚 Paragraph 控件（静态内容，无需刷新）。
func NewFooter() *widgets.Paragraph {
	p := widgets.NewParagraph()
	p.Text = " [Ctrl+C / Q] 停止 | [P] 暂停刷新 | [S] 审计快照 | Sentinel-Flow v2.0"
	p.Border = false
	return p
}
