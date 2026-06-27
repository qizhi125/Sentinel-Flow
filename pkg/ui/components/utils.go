// Sentinel-Flow NIDS — 工具函数（净化、截断、填充、Sparkline、等级标记）。
package components

import (
	"fmt"
	"regexp"
	"strings"
	"time"
	"unicode"

	"github.com/mattn/go-runewidth"
)

var nonprintRE = regexp.MustCompile(`[^[:print:]]`)

// Sanitize 将不可打印字符替换为 "."，防止原始报文破坏终端渲染。
func Sanitize(s string) string {
	return nonprintRE.ReplaceAllString(s, ".")
}

const sparkChars = " ▂▃▅██▅▃▂"

// Sparkline 将 uint64 序列渲染为单行 ASCII sparkline 字符串。
func Sparkline(vals []uint64, maxV uint64) string {
	if maxV == 0 {
		return strings.Repeat(" ", len(vals))
	}
	var b strings.Builder
	for _, v := range vals {
		idx := int(v * uint64(len(sparkChars)-1) / maxV)
		if idx >= len(sparkChars) {
			idx = len(sparkChars) - 1
		}
		b.WriteByte(sparkChars[idx])
	}
	return b.String()
}

// PadNum 将 uint64 右对齐填充到指定宽度。
func PadNum(v uint64, w int) string {
	s := fmt.Sprintf("%d", v)
	if len(s) >= w {
		return s
	}
	return strings.Repeat(" ", w-len(s)) + s
}

// PadDur 将 time.Duration 格式化为字符串并右对齐填充到指定宽度。
func PadDur(d time.Duration, w int) string {
	s := d.Round(time.Second).String()
	if len(s) >= w {
		return s
	}
	return strings.Repeat(" ", w-len(s)) + s
}

// SafeTrunc 使用 runewidth 安全截断字符串，不破坏 UTF-8 边界。
func SafeTrunc(s string, w int) string {
	if runewidth.StringWidth(s) <= w {
		return s
	}
	var b strings.Builder
	c := 0
	for _, r := range s {
		rw := runewidth.RuneWidth(r)
		if c+rw > w-1 {
			b.WriteRune('…')
			break
		}
		b.WriteRune(r)
		c += rw
	}
	return b.String()
}

// PadRune 将字符串填充到指定 runewidth 宽度（右侧空格补齐）。
func PadRune(s string, w int) string {
	rw := runewidth.StringWidth(s)
	if rw >= w {
		return SafeTrunc(s, w)
	}
	return s + strings.Repeat(" ", w-rw)
}

// SevShort 返回紧凑的告警等级标记（用于表格 Sev 列）。
func SevShort(lvl string) string {
	switch lvl {
	case "CRIT":
		return "🔴CRIT"
	case "HIGH":
		return "🔴HIGH"
	case "MED":
		return "🟡MED"
	case "LOW":
		return "🟢LOW"
	default:
		return "●INFO"
	}
}

func JoinLines(lines ...string) string {
	return strings.Join(lines, "\n")
}

func init() {
	_ = nonprintRE.MatchString("")
	_ = unicode.IsPrint('a')
}
