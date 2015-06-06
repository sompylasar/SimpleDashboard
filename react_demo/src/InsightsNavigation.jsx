;(function (root, factory) {
  root.InsightsNavigation = factory(
    root.React
  );
})(this, function (React) {
  // Navigation bar.
  return React.createClass({
    render: function () {
      var _this = this;

      // HACK(sompylasar): Simplest error handling.
      try {
        var navData = _this.props.navData;

        return <div className="c5t-insights-nav">
          <table border="0" align="center" cellPadding="8">
            <tbody>
              <tr align="center">
                <td><a href={navData.previous_url + '&html=yes'}>Previous insight</a></td>
                <td><a href={navData.next_url + '&html=yes'}>Next insight</a></td>
              </tr>
            </tbody>
          </table>
        </div>;
      }
      catch (ex) {
        return <div className="c5t-insights-nav">
          <div className="c5t-error">
           An error occurred: <span>{ex.message}</span>
          </div>
        </div>;
      }
    }
  });
});