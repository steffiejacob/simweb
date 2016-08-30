#!/usr/bin/env sh

# set the working directories
SIMWEBDIR=$1
TOPSITES=$1/topsites

while read -r line; do
  SERVICE="$line"

  # run simweb
  ( $SIMWEBDIR/.build/webget \
    --simweb \
    --ipversion=6 \
    --user_agent="Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.0)" \
    $SERVICE 2> /dev/null ) |
    tee -a $SIMWEBDIR/simweb.txt 2> /dev/null

  ( $SIMWEBDIR/.build/webget \
    --simweb \
    --ipversion=4 \
    --user_agent="Mozilla/4.0 (compatible; MSIE 8.0; Windows NT 6.0)" \
    $SERVICE 2> /dev/null ) |
    tee -a $SIMWEBDIR/simweb.txt 2> /dev/null

done < $TOPSITES
