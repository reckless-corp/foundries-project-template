FROM busybox:stable
COPY matrix.sh matrix.html /
EXPOSE 8080
CMD ["tcpsvd", "-v", "-c", "20", "0.0.0.0", "8080", "/bin/sh", "/matrix.sh"]
