// Package web 内嵌 Go 控制面提供的静态前端。
package web

import "embed"

//go:embed index.html style.css
var Assets embed.FS
