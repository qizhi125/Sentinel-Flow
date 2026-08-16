// Command sentinel-web 是 Go 控制面：HTTP ingest、SSE 事件流与嵌入式前端。
package main

import (
	"crypto/tls"
	"crypto/x509"
	"flag"
	"fmt"
	"log"
	"net"
	"net/http"
	"os"
	"os/exec"
	"time"

	"github.com/qizhi125/Sentinel-Flow/go/internal/httpapi"
	"github.com/qizhi125/Sentinel-Flow/go/internal/ringbuffer"
)

func main() {
	addr := flag.String("addr", "127.0.0.1:21318", "HTTP 监听地址")
	noOpen := flag.Bool("no-open", false, "启动时不自动打开浏览器")
	tlsCert := flag.String("tls-cert", "", "服务端证书文件（对外暴露时必填）")
	tlsKey := flag.String("tls-key", "", "服务端私钥文件（对外暴露时必填）")
	clientCA := flag.String("client-ca", "", "客户端 CA 文件（mTLS）")
	flag.Parse()
	useTLS := *tlsCert != "" || *tlsKey != "" || *clientCA != ""
	if err := validateExpose(*addr, useTLS); err != nil {
		log.Fatal(err)
	}

	rb := ringbuffer.New(1000)
	server := &http.Server{
		Addr:              *addr,
		Handler:           httpapi.NewHandler(rb),
		ReadHeaderTimeout: 5 * time.Second,
	}
	url := "http://" + *addr
	log.Printf("sentinel-web 正在监听 %s", url)
	if !*noOpen {
		openBrowser(url)
	}
	var err error
	if useTLS {
		if *tlsCert == "" || *tlsKey == "" || *clientCA == "" {
			log.Fatal("mTLS 需要 --tls-cert、--tls-key、--client-ca 三者齐全")
		}
		caPEM, readErr := os.ReadFile(*clientCA)
		if readErr != nil {
			log.Fatalf("读取客户端 CA 失败: %v", readErr)
		}
		pool := x509.NewCertPool()
		if !pool.AppendCertsFromPEM(caPEM) {
			log.Fatal("客户端 CA 文件中没有有效证书")
		}
		server.TLSConfig = &tls.Config{
			MinVersion: tls.VersionTLS12,
			ClientCAs:  pool,
			ClientAuth: tls.RequireAndVerifyClientCert,
		}
		err = server.ListenAndServeTLS(*tlsCert, *tlsKey)
	} else {
		err = server.ListenAndServe()
	}
	if err != nil {
		log.Fatal(err)
	}
}

// validateExpose 非回环绑定必须启用 mTLS，否则拒绝启动。
func validateExpose(addr string, useTLS bool) error {
	host, _, err := net.SplitHostPort(addr)
	if err != nil {
		return fmt.Errorf("监听地址格式错误: %w", err)
	}
	switch host {
	case "", "127.0.0.1", "localhost", "::1":
		return nil
	}
	if !useTLS {
		return fmt.Errorf("非回环绑定 %s 必须启用 mTLS（--tls-cert/--tls-key/--client-ca）", addr)
	}
	return nil
}

// openBrowser 尽力打开默认浏览器；失败只记录日志，不影响服务启动。
func openBrowser(url string) {
	cmd := exec.Command("xdg-open", url)
	if err := cmd.Start(); err != nil {
		log.Printf("自动打开浏览器失败: %v", err)
		return
	}
	go func() {
		_ = cmd.Wait()
	}()
}
