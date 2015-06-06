;(function (root, factory) {
  root.InsightsBrowser = factory(
    root.React,
    root.InsightsNavigation,
    root.MutualInformationInsightsVisualizer
  );
})(this, function (React, InsightsNavigation, MutualInformationInsightsVisualizer) {
  return React.createClass({
    render: function () {
      var _this = this;

      // HACK(sompylasar): Simplest error handling.
      try {
        var data = _this.props.data;

        var navData = data.data;

        var insightCereal = data.data.insight;
        var insightType = insightCereal.polymorphic_name;
        var insightData = insightCereal.ptr_wrapper.data;

        var insightTypeToVisualizerMap = {
          "insight::MutualInformation": MutualInformationInsightsVisualizer
        };

        var Visualizer = insightTypeToVisualizerMap[insightType];

        if ( !Visualizer ) {
          return <div className="c5t-insights-browser">
            <InsightsNavigation navData={navData} />
            <div className="c5t-error">
              No visualizer found for `<span>{insightType}</span>`.
            </div>
          </div>;
        }

        return <div className="c5t-insights-browser">
          <InsightsNavigation navData={navData} />
          <Visualizer insightData={insightData} />
        </div>;
      }
      catch (ex) {
        return <div className="c5t-insights-browser">
          <div className="c5t-error">
           An error occurred: <span>{ex.message}</span>
          </div>
        </div>;
      }
    }
  });
});