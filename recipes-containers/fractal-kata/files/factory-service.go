// SPDX-License-Identifier: Apache-2.0
// The appliance factory service links Kata's VM factory API directly.
package main

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"net"
	"net/http"
	"os"
	"os/signal"
	"strconv"
	"syscall"
	"time"

	"github.com/kata-containers/kata-containers/src/runtime/pkg/katautils"
	vc "github.com/kata-containers/kata-containers/src/runtime/virtcontainers"
	vf "github.com/kata-containers/kata-containers/src/runtime/virtcontainers/factory"
)

const templatePath = "/run/kata-containers/template"
const socketPath = "/run/fractal-vm-factory.sock"

type status struct {
	Enabled   bool      `json:"enabled"`
	Ready     bool      `json:"ready"`
	Created   time.Time `json:"created,omitempty"`
	MemoryMiB uint32    `json:"memory_mib,omitempty"`
}

func run() error {
	ctx, stop := signal.NotifyContext(context.Background(), syscall.SIGTERM, syscall.SIGINT)
	defer stop()
	var state status
	data, err := os.ReadFile("/run/kata-containers/factory.json")
	if err != nil {
		return err
	}
	if err := json.Unmarshal(data, &state); err != nil {
		return err
	}
	if state.Enabled {
		_, cfg, err := katautils.LoadConfiguration("/run/kata-containers/configuration.toml", true)
		if err != nil {
			return err
		}
		if !cfg.FactoryConfig.Template || cfg.FactoryConfig.TemplatePath != templatePath || cfg.HypervisorConfig.SharedFS != "none" {
			return errors.New("invalid appliance factory profile")
		}
		// The supervisor stops all consumers before restarting this service.
		// A prior failed process may leave its private template tmpfs behind.
		if st, err := os.Lstat(templatePath); err == nil {
			if !st.IsDir() {
				return errors.New("invalid template directory")
			}
			if err := syscall.Unmount(templatePath, 0); err != nil && err != syscall.EINVAL && err != syscall.ENOENT {
				return err
			}
			if err := os.RemoveAll(templatePath); err != nil {
				return err
			}
		} else if !os.IsNotExist(err) {
			return err
		}
		prepare, cancel := context.WithTimeout(ctx, 90*time.Second)
		factory, err := vf.NewFactory(prepare, vf.Config{Template: true, TemplatePath: templatePath, VMConfig: vc.VMConfig{
			HypervisorType: cfg.HypervisorType, HypervisorConfig: cfg.HypervisorConfig, AgentConfig: cfg.AgentConfig,
		}}, false)
		cancel()
		if err != nil {
			return err
		}
		defer factory.CloseFactory(context.Background())
		state.Ready, state.Created, state.MemoryMiB = true, time.Now().UTC(), cfg.HypervisorConfig.MemorySize
	}
	if err := os.Remove(socketPath); err != nil && !os.IsNotExist(err) {
		return err
	}
	listener, err := net.Listen("unix", socketPath)
	if err != nil {
		return err
	}
	defer listener.Close()
	defer os.Remove(socketPath)
	if err := os.Chmod(socketPath, 0600); err != nil {
		return err
	}
	mux := http.NewServeMux()
	mux.HandleFunc("GET /status", func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "application/json")
		_ = json.NewEncoder(w).Encode(state)
	})
	mux.HandleFunc("POST /prepare", func(w http.ResponseWriter, r *http.Request) {
		if !state.Enabled || !state.Ready {
			http.Error(w, "VM factory is disabled", http.StatusConflict)
			return
		}
		w.Header().Set("Content-Type", "application/json")
		_ = json.NewEncoder(w).Encode(state)
	})
	server := &http.Server{Handler: mux, ReadHeaderTimeout: 5 * time.Second, ReadTimeout: 5 * time.Second, WriteTimeout: 5 * time.Second, IdleTimeout: 10 * time.Second, MaxHeaderBytes: 4096}
	done := make(chan error, 1)
	go func() { done <- server.Serve(listener) }()
	if fd, err := strconv.Atoi(os.Getenv("FCR_READY_FD")); err == nil {
		f := os.NewFile(uintptr(fd), "ready")
		if _, err := f.Write([]byte{'R'}); err != nil {
			return err
		}
		_ = f.Close()
		_ = os.Unsetenv("FCR_READY_FD")
	}
	select {
	case <-ctx.Done():
		closeCtx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		defer cancel()
		return server.Shutdown(closeCtx)
	case err := <-done:
		return err
	}
}

func main() {
	if err := run(); err != nil {
		fmt.Fprintln(os.Stderr, err)
		os.Exit(1)
	}
}
