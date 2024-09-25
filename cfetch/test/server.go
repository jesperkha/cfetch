package main

import (
	"log"
	"net/http"
)

func main() {
	mux := http.NewServeMux()

	mux.Handle("/", http.HandlerFunc(func(res http.ResponseWriter, req *http.Request) {
		res.Write([]byte("Hello from server!"))
	}))

	log.Println("Listening at localhost:8080")
	log.Fatal(http.ListenAndServe("127.0.0.1:8080", mux))
}
